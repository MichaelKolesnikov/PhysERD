#include "DbmlParser.h"
#include "DbmlLexer.h"
#include <QSet>
#include <QHash>
#include <algorithm>

namespace
{
   struct Setting
   {
      QString key; // lowercase, words joined by one space: "not null", "primary key"
      DbmlToken keyToken;
      int valueBegin = -1; // token range of the value, valueBegin == valueEnd if there is no value
      int valueEnd = -1;

      bool hasValue() const
      {
         return valueBegin >= 0 && valueBegin < valueEnd;
      }
   };

   // ~partial_name inside a table body
   struct Injection
   {
      int ownColumnsBefore = 0;
      QString partial;
      DbmlToken token;
   };

   struct PendingTable
   {
      TableDTO table;
      QVector<Injection> injections;
      bool ownHeaderColor = false;
      bool ownNote = false;
   };

   struct Partial
   {
      TableDTO table;
      QVector<RefDTO> inlineRefs; // left endpoint is filled in for each table that injects the partial
   };

   struct PendingGroupTable
   {
      int group = 0;
      QString schema;
      QString name;
      DbmlToken token;
   };

   class Parser
   {
   public:
      Parser(const QString& source, DbmlParseResult& result)
         : m_source(source), m_result(result), m_schema(result.schema)
      {
         m_tokens = DbmlLexer::tokenize(source, &m_result.errors);
      }

      void run()
      {
         while (true)
         {
            skipNewlines();
            if (peek().kind == DbmlToken::End)
            {
               break;
            }
            const int start = m_pos;
            if (!parseElement())
            {
               skipElement(start);
            }
         }

         resolvePartials();
         validateTables();
         resolveRefs();
         resolveGroups();

         std::stable_sort(m_result.errors.begin(), m_result.errors.end(), [](const DbmlError& a, const DbmlError& b) {
            return a.line != b.line ? a.line < b.line : a.column < b.column;
         });
      }

   private:
      // ---- token access ----

      const DbmlToken& peek(int offset = 0) const
      {
         const int index = std::max(0, std::min(m_pos + offset, m_tokens.size() - 1));
         return m_tokens[index];
      }

      const DbmlToken& next()
      {
         const DbmlToken& token = m_tokens[m_pos];
         if (m_pos < m_tokens.size() - 1)
         {
            ++m_pos;
         }
         return token;
      }

      bool isSymbol(const char* symbol, int offset = 0) const
      {
         const DbmlToken& token = peek(offset);
         return token.kind == DbmlToken::Symbol && token.text == QLatin1String(symbol);
      }

      bool isKeyword(const char* keyword, int offset = 0) const
      {
         const DbmlToken& token = peek(offset);
         return token.kind == DbmlToken::Identifier && token.text.compare(QLatin1String(keyword), Qt::CaseInsensitive) == 0;
      }

      static bool isName(const DbmlToken& token)
      {
         return token.kind == DbmlToken::Identifier || token.kind == DbmlToken::QuotedIdentifier;
      }

      bool atLineEnd() const
      {
         const DbmlToken& token = peek();
         return token.kind == DbmlToken::Newline || token.kind == DbmlToken::End || isSymbol("}");
      }

      void skipNewlines()
      {
         while (peek().kind == DbmlToken::Newline)
         {
            next();
         }
      }

      static QString describe(const DbmlToken& token)
      {
         switch (token.kind)
         {
         case DbmlToken::End:
            return QStringLiteral("end of file");
         case DbmlToken::Newline:
            return QStringLiteral("end of line");
         case DbmlToken::String:
            return QStringLiteral("string '%1'").arg(token.text);
         default:
            return QStringLiteral("'%1'").arg(token.text);
         }
      }

      void error(const DbmlToken& token, const QString& message)
      {
         m_result.errors.append(DbmlError{token.line, token.column, std::max(1, token.length), message});
      }

      void errorAtLine(int line, const QString& message)
      {
         m_result.errors.append(DbmlError{line, 1, 0, message});
      }

      bool expectSymbol(const char* symbol, const QString& context)
      {
         if (isSymbol(symbol))
         {
            next();
            return true;
         }
         error(peek(), QStringLiteral("Expected '%1' %2, found %3").arg(QLatin1String(symbol), context, describe(peek())));
         return false;
      }

      // Skips the rest of a line inside a block. A '}' is left for the block
      void skipLine()
      {
         int depth = 0;
         while (true)
         {
            const DbmlToken& token = peek();
            if (token.kind == DbmlToken::End)
            {
               return;
            }
            if (token.kind == DbmlToken::Newline && depth == 0)
            {
               next();
               return;
            }
            if (token.kind == DbmlToken::Symbol)
            {
               if (token.text == '[' || token.text == '(')
               {
                  ++depth;
               }
               else if (token.text == ']' || token.text == ')')
               {
                  depth = std::max(0, depth - 1);
               }
               else if (token.text == '}' && depth == 0)
               {
                  return;
               }
            }
            next();
         }
      }

      // Every line inside a block ends with a line break or the closing brace
      void expectLineEnd()
      {
         if (peek().kind == DbmlToken::Newline)
         {
            next();
            return;
         }
         if (atLineEnd())
         {
            return;
         }
         error(peek(), QStringLiteral("Unexpected %1").arg(describe(peek())));
         skipLine();
      }

      // Skips a broken top-level element from its first token: up to its closing brace, or its line if it has no body
      void skipElement(int start)
      {
         m_pos = start;
         int depth = 0;
         bool seenBrace = false;
         while (peek().kind != DbmlToken::End)
         {
            const DbmlToken& token = next();
            if (token.kind == DbmlToken::Symbol && token.text == '{')
            {
               ++depth;
               seenBrace = true;
            }
            else if (token.kind == DbmlToken::Symbol && token.text == '}')
            {
               --depth;
               if (depth <= 0 && seenBrace)
               {
                  return;
               }
            }
            else if (token.kind == DbmlToken::Newline && depth <= 0 && !seenBrace)
            {
               skipNewlines();
               if (!isSymbol("{"))
               {
                  return;
               }
            }
         }
      }

      // ---- generic pieces ----

      // name or schema.name
      bool parseQualifiedName(QString& schema, QString& name, const QString& what)
      {
         if (!isName(peek()))
         {
            error(peek(), QStringLiteral("Expected %1 name, found %2").arg(what, describe(peek())));
            return false;
         }
         QStringList parts{next().text};
         while (isSymbol(".") && isName(peek(1)))
         {
            next();
            parts << next().text;
         }
         if (parts.size() > 2)
         {
            error(peek(-1), QStringLiteral("Too many parts in %1 name '%2'").arg(what, parts.join('.')));
            return false;
         }
         name = parts.last();
         schema = parts.size() == 2 ? parts.first() : QString();
         return true;
      }

      QVector<Setting> parseSettings()
      {
         QVector<Setting> settings;
         next(); // [
         while (true)
         {
            if (isSymbol("]"))
            {
               next();
               return settings;
            }
            // settings stay on one line, so a missing ']' does not swallow the next column
            if (peek().kind == DbmlToken::End || peek().kind == DbmlToken::Newline || isSymbol("}"))
            {
               error(peek(), QStringLiteral("Expected ']' to close the settings"));
               return settings;
            }

            Setting setting;
            setting.keyToken = peek();
            QStringList words;
            while (peek().kind == DbmlToken::Identifier)
            {
               words << next().text.toLower();
            }
            if (words.isEmpty())
            {
               error(peek(), QStringLiteral("Expected a setting name, found %1").arg(describe(peek())));
               while (!isSymbol(",") && !isSymbol("]") && peek().kind != DbmlToken::End && !isSymbol("}"))
               {
                  next();
               }
               if (isSymbol(","))
               {
                  next();
               }
               continue;
            }
            setting.key = words.join(' ');
            setting.keyToken.length = peek(-1).position + peek(-1).length - setting.keyToken.position;

            if (isSymbol(":"))
            {
               next();
               setting.valueBegin = m_pos;
               int depth = 0;
               while (peek().kind != DbmlToken::End && peek().kind != DbmlToken::Newline)
               {
                  if (depth == 0 && (isSymbol(",") || isSymbol("]")))
                  {
                     break;
                  }
                  if (isSymbol("("))
                  {
                     ++depth;
                  }
                  else if (isSymbol(")"))
                  {
                     --depth;
                  }
                  next();
               }
               setting.valueEnd = m_pos;
               if (!setting.hasValue())
               {
                  error(setting.keyToken, QStringLiteral("Setting '%1' needs a value").arg(setting.key));
               }
            }
            settings << setting;

            if (isSymbol(","))
            {
               next();
            }
            else if (!isSymbol("]") && peek().kind != DbmlToken::Newline)
            {
               error(peek(), QStringLiteral("Expected ',' or ']' in settings, found %1").arg(describe(peek())));
               while (!isSymbol(",") && !isSymbol("]") && peek().kind != DbmlToken::End && !isSymbol("}"))
               {
                  next();
               }
               if (isSymbol(","))
               {
                  next();
               }
            }
         }
      }

      // The value of a setting as text: a single token, a negative number or words like "set null"
      QString settingText(const Setting& setting) const
      {
         if (!setting.hasValue())
         {
            return QString();
         }
         QStringList parts;
         for (int i = setting.valueBegin; i < setting.valueEnd; ++i)
         {
            parts << m_tokens[i].text;
         }
         if (parts.size() == 2 && parts.first() == '-')
         {
            return parts.join(QString());
         }
         return parts.join(' ');
      }

      const DbmlToken& settingToken(const Setting& setting) const
      {
         return setting.hasValue() ? m_tokens[setting.valueBegin] : setting.keyToken;
      }

      // A setting whose value must be one string: note: '...'
      bool settingString(const Setting& setting, QString& out)
      {
         if (setting.hasValue() && setting.valueEnd - setting.valueBegin == 1 && m_tokens[setting.valueBegin].kind == DbmlToken::String)
         {
            out = m_tokens[setting.valueBegin].text;
            return true;
         }
         if (setting.hasValue())
         {
            error(settingToken(setting), QStringLiteral("Setting '%1' expects a string in single quotes").arg(setting.key));
         }
         return false;
      }

      bool settingColor(const Setting& setting, QString& out)
      {
         if (setting.hasValue() && setting.valueEnd - setting.valueBegin == 1 && m_tokens[setting.valueBegin].kind == DbmlToken::Color)
         {
            out = m_tokens[setting.valueBegin].text;
            return true;
         }
         if (setting.hasValue())
         {
            error(settingToken(setting), QStringLiteral("Setting '%1' expects a color like #3498db").arg(setting.key));
         }
         return false;
      }

      void unknownSetting(const Setting& setting, const QString& where)
      {
         error(setting.keyToken, QStringLiteral("Unknown %1 setting '%2'").arg(where, setting.key));
      }

      // Note: '...' or Note { '...' }, the Note keyword is current
      bool parseNoteValue(QString& out)
      {
         next(); // Note
         if (isSymbol(":"))
         {
            next();
            if (peek().kind != DbmlToken::String)
            {
               error(peek(), QStringLiteral("Expected a note string in single quotes, found %1").arg(describe(peek())));
               return false;
            }
            out = next().text;
            return true;
         }
         if (isSymbol("{"))
         {
            next();
            skipNewlines();
            if (peek().kind != DbmlToken::String)
            {
               error(peek(), QStringLiteral("Expected a note string in single quotes, found %1").arg(describe(peek())));
               return false;
            }
            out = next().text;
            skipNewlines();
            return expectSymbol("}", QStringLiteral("to close the note"));
         }
         error(peek(), QStringLiteral("Expected ':' or '{' after Note"));
         return false;
      }

      bool isNoteLine() const
      {
         return isKeyword("note") && (isSymbol(":", 1) || isSymbol("{", 1));
      }

      // ---- top level ----

      bool parseElement()
      {
         if (isKeyword("table"))
         {
            return parseTable(false);
         }
         if (isKeyword("tablepartial"))
         {
            return parseTable(true);
         }
         if (isKeyword("ref"))
         {
            return parseRefStatement();
         }
         if (isKeyword("enum"))
         {
            return parseEnum();
         }
         if (isKeyword("tablegroup"))
         {
            return parseTableGroup();
         }
         if (isKeyword("project"))
         {
            return parseProject();
         }
         if (isKeyword("note"))
         {
            return parseStickyNote();
         }
         error(peek(), QStringLiteral("Unexpected %1: expected Table, Ref, Enum, TableGroup, Project or Note").arg(describe(peek())));
         return false;
      }

      bool parseTable(bool isPartial)
      {
         const DbmlToken keyword = next();
         PendingTable pending;
         TableDTO& table = pending.table;
         table.line = keyword.line;
         if (!parseQualifiedName(table.schema, table.name, isPartial ? QStringLiteral("partial") : QStringLiteral("table")))
         {
            return false;
         }
         if (isKeyword("as"))
         {
            next();
            if (!isName(peek()))
            {
               error(peek(), QStringLiteral("Expected an alias after 'as'"));
               return false;
            }
            table.alias = next().text;
            if (isPartial)
            {
               error(peek(-1), QStringLiteral("A table partial cannot have an alias"));
            }
         }
         if (isSymbol("["))
         {
            for (const Setting& setting : parseSettings())
            {
               if (setting.key == QLatin1String("headercolor"))
               {
                  pending.ownHeaderColor = settingColor(setting, table.headerColor);
               }
               else if (setting.key == QLatin1String("note"))
               {
                  pending.ownNote = settingString(setting, table.note);
               }
               else
               {
                  unknownSetting(setting, QStringLiteral("table"));
               }
            }
         }
         skipNewlines();
         if (!expectSymbol("{", QStringLiteral("to open the table body")))
         {
            return false;
         }

         QVector<RefDTO> inlineRefs;
         while (true)
         {
            skipNewlines();
            if (isSymbol("}"))
            {
               table.endLine = next().line;
               break;
            }
            if (peek().kind == DbmlToken::End)
            {
               error(keyword, QStringLiteral("Table '%1' is not closed with '}'").arg(table.name));
               table.endLine = peek().line;
               break;
            }
            if (isNoteLine())
            {
               QString note;
               if (parseNoteValue(note))
               {
                  table.note = note;
                  pending.ownNote = true;
                  expectLineEnd();
               }
               else
               {
                  skipLine();
               }
            }
            else if (isKeyword("indexes") && isSymbol("{", 1))
            {
               parseIndexes(table);
            }
            else if (isSymbol("~"))
            {
               const DbmlToken tilde = next();
               if (!isName(peek()))
               {
                  error(peek(), QStringLiteral("Expected a partial name after '~'"));
                  skipLine();
                  continue;
               }
               Injection injection;
               injection.ownColumnsBefore = table.columns.size();
               injection.token = next();
               injection.token.column = tilde.column;
               injection.partial = injection.token.text;
               if (isPartial)
               {
                  error(tilde, QStringLiteral("A table partial cannot inject another partial"));
               }
               else
               {
                  pending.injections << injection;
               }
               expectLineEnd();
            }
            else
            {
               parseColumn(table, inlineRefs);
            }
         }

         if (isPartial)
         {
            if (m_partials.contains(table.name))
            {
               error(keyword, QStringLiteral("Table partial '%1' is already defined").arg(table.name));
            }
            m_partials.insert(table.name, Partial{table, inlineRefs});
         }
         else
         {
            m_tables << pending;
            m_schema.refs << inlineRefs;
         }
         return true;
      }

      void parseColumn(TableDTO& table, QVector<RefDTO>& inlineRefs)
      {
         const DbmlToken nameToken = peek();
         if (!isName(nameToken))
         {
            error(nameToken, QStringLiteral("Expected a column name, found %1").arg(describe(nameToken)));
            skipLine();
            return;
         }
         next();

         ColumnDTO column;
         column.name = nameToken.text;
         column.line = nameToken.line;
         if (!parseType(column))
         {
            skipLine();
            return;
         }

         if (isSymbol("["))
         {
            for (const Setting& setting : parseSettings())
            {
               applyColumnSetting(table, column, setting, inlineRefs);
            }
         }
         expectLineEnd();
         table.columns << column;
      }

      bool parseType(ColumnDTO& column)
      {
         if (!isName(peek()))
         {
            error(peek(), QStringLiteral("Expected a type for column '%1', found %2").arg(column.name, describe(peek())));
            return false;
         }
         column.type = next().text;
         while (isSymbol(".") && isName(peek(1)))
         {
            next();
            column.type += '.' + next().text;
         }
         if (isSymbol("("))
         {
            const int begin = peek().position;
            int depth = 0;
            while (peek().kind != DbmlToken::End && peek().kind != DbmlToken::Newline)
            {
               if (isSymbol("("))
               {
                  ++depth;
               }
               else if (isSymbol(")"))
               {
                  --depth;
                  if (depth == 0)
                  {
                     break;
                  }
               }
               next();
            }
            if (!isSymbol(")"))
            {
               error(peek(), QStringLiteral("Expected ')' to close the type arguments of '%1'").arg(column.name));
               return false;
            }
            const DbmlToken& close = next();
            QString arguments = m_source.mid(begin, close.position + 1 - begin);
            arguments.remove(' ');
            column.type += arguments;
         }
         while (isSymbol("[") && isSymbol("]", 1))
         {
            next();
            next();
            column.type += QStringLiteral("[]");
         }
         return true;
      }

      void applyColumnSetting(const TableDTO& table, ColumnDTO& column, const Setting& setting, QVector<RefDTO>& inlineRefs)
      {
         const QString& key = setting.key;
         if (key == QLatin1String("pk") || key == QLatin1String("primary key"))
         {
            column.pk = true;
         }
         else if (key == QLatin1String("increment"))
         {
            column.increment = true;
         }
         else if (key == QLatin1String("not null"))
         {
            column.notNull = true;
         }
         else if (key == QLatin1String("null"))
         {
            column.notNull = false;
         }
         else if (key == QLatin1String("unique"))
         {
            column.unique = true;
         }
         else if (key == QLatin1String("note"))
         {
            settingString(setting, column.note);
         }
         else if (key == QLatin1String("default"))
         {
            parseDefault(column, setting);
         }
         else if (key == QLatin1String("ref"))
         {
            if (!setting.hasValue())
            {
               return;
            }
            const int saved = m_pos;
            m_pos = setting.valueBegin;
            RefDTO ref;
            ref.isInline = true;
            ref.line = setting.keyToken.line;
            ref.left.schema = table.schema;
            ref.left.table = table.name;
            ref.left.columns = QStringList{column.name};
            if (parseRelation(ref) && parseEndpoint(ref.right))
            {
               if (m_pos != setting.valueEnd)
               {
                  error(peek(), QStringLiteral("Unexpected %1 in ref").arg(describe(peek())));
               }
               else if (ref.right.columns.size() != 1)
               {
                  error(setting.keyToken, QStringLiteral("An inline ref must point to a single column"));
               }
               else
               {
                  inlineRefs << ref;
               }
            }
            m_pos = saved;
         }
         else if (key == QLatin1String("check"))
         {
            // a check constraint does not affect the diagram
         }
         else
         {
            unknownSetting(setting, QStringLiteral("column"));
         }
      }

      void parseDefault(ColumnDTO& column, const Setting& setting)
      {
         if (!setting.hasValue())
         {
            return;
         }
         const DbmlToken& first = m_tokens[setting.valueBegin];
         const int count = setting.valueEnd - setting.valueBegin;
         if (count == 2 && first.kind == DbmlToken::Symbol && first.text == '-' && m_tokens[setting.valueBegin + 1].kind == DbmlToken::Number)
         {
            column.defaultKind = ColumnDTO::NumberDefault;
            column.defaultValue = '-' + m_tokens[setting.valueBegin + 1].text;
            return;
         }
         if (count != 1)
         {
            error(first, QStringLiteral("Invalid default value: use 'text', a number, true/false/null or `expression`"));
            return;
         }
         column.defaultValue = first.text;
         switch (first.kind)
         {
         case DbmlToken::String:
            column.defaultKind = ColumnDTO::StringDefault;
            break;
         case DbmlToken::Expression:
            column.defaultKind = ColumnDTO::ExpressionDefault;
            break;
         case DbmlToken::Number:
            column.defaultKind = ColumnDTO::NumberDefault;
            break;
         case DbmlToken::Identifier:
            if (first.text.compare(QLatin1String("true"), Qt::CaseInsensitive) == 0 || first.text.compare(QLatin1String("false"), Qt::CaseInsensitive) == 0)
            {
               column.defaultKind = ColumnDTO::BooleanDefault;
               column.defaultValue = first.text.toLower();
            }
            else if (first.text.compare(QLatin1String("null"), Qt::CaseInsensitive) == 0)
            {
               column.defaultKind = ColumnDTO::NullDefault;
               column.defaultValue = QStringLiteral("null");
            }
            else
            {
               column.defaultKind = ColumnDTO::ExpressionDefault;
            }
            break;
         default:
            error(first, QStringLiteral("Invalid default value %1").arg(describe(first)));
            column.defaultValue.clear();
            break;
         }
      }

      void parseIndexes(TableDTO& table)
      {
         next(); // indexes
         next(); // {
         while (true)
         {
            skipNewlines();
            if (isSymbol("}"))
            {
               next();
               expectLineEnd();
               return;
            }
            if (peek().kind == DbmlToken::End)
            {
               error(peek(), QStringLiteral("Expected '}' to close the indexes"));
               return;
            }

            IndexDTO index;
            index.line = peek().line;
            if (isSymbol("("))
            {
               next();
               bool ok = true;
               while (true)
               {
                  skipNewlines();
                  if (isName(peek()))
                  {
                     index.columns << IndexColumnDTO{next().text, false};
                  }
                  else if (peek().kind == DbmlToken::Expression)
                  {
                     index.columns << IndexColumnDTO{next().text, true};
                  }
                  else
                  {
                     error(peek(), QStringLiteral("Expected a column name or `expression` in the index, found %1").arg(describe(peek())));
                     ok = false;
                     break;
                  }
                  if (isSymbol(","))
                  {
                     next();
                     continue;
                  }
                  if (isSymbol(")"))
                  {
                     next();
                     break;
                  }
                  error(peek(), QStringLiteral("Expected ',' or ')' in the index, found %1").arg(describe(peek())));
                  ok = false;
                  break;
               }
               if (!ok)
               {
                  skipLine();
                  continue;
               }
            }
            else if (isName(peek()))
            {
               index.columns << IndexColumnDTO{next().text, false};
            }
            else if (peek().kind == DbmlToken::Expression)
            {
               index.columns << IndexColumnDTO{next().text, true};
            }
            else
            {
               error(peek(), QStringLiteral("Expected an index definition, found %1").arg(describe(peek())));
               skipLine();
               continue;
            }

            if (isSymbol("["))
            {
               for (const Setting& setting : parseSettings())
               {
                  if (setting.key == QLatin1String("pk") || setting.key == QLatin1String("primary key"))
                  {
                     index.pk = true;
                  }
                  else if (setting.key == QLatin1String("unique"))
                  {
                     index.unique = true;
                  }
                  else if (setting.key == QLatin1String("name"))
                  {
                     settingString(setting, index.name);
                  }
                  else if (setting.key == QLatin1String("note"))
                  {
                     settingString(setting, index.note);
                  }
                  else if (setting.key == QLatin1String("type"))
                  {
                     index.type = settingText(setting).toLower();
                     if (index.type != QLatin1String("btree") && index.type != QLatin1String("hash"))
                     {
                        error(settingToken(setting), QStringLiteral("Index type must be btree or hash"));
                     }
                  }
                  else
                  {
                     unknownSetting(setting, QStringLiteral("index"));
                  }
               }
            }
            expectLineEnd();
            table.indexes << index;
         }
      }

      bool parseRelation(RefDTO& ref)
      {
         const DbmlToken& token = peek();
         if (token.kind == DbmlToken::Symbol)
         {
            if (token.text == '>')
            {
               ref.leftCardinality = Cardinality::Many;
               ref.rightCardinality = Cardinality::One;
            }
            else if (token.text == '<')
            {
               ref.leftCardinality = Cardinality::One;
               ref.rightCardinality = Cardinality::Many;
            }
            else if (token.text == '-')
            {
               ref.leftCardinality = Cardinality::One;
               ref.rightCardinality = Cardinality::One;
            }
            else if (token.text == QLatin1String("<>"))
            {
               ref.leftCardinality = Cardinality::Many;
               ref.rightCardinality = Cardinality::Many;
            }
            else
            {
               error(token, QStringLiteral("Expected a relationship: <, >, - or <>"));
               return false;
            }
            next();
            return true;
         }
         error(token, QStringLiteral("Expected a relationship (<, >, - or <>), found %1").arg(describe(token)));
         return false;
      }

      // table.column, schema.table.column, table.(column1, column2)
      bool parseEndpoint(RefEndpointDTO& endpoint)
      {
         QStringList parts;
         bool composite = false;
         while (true)
         {
            if (isSymbol("(") && !parts.isEmpty())
            {
               next();
               while (true)
               {
                  if (!isName(peek()))
                  {
                     error(peek(), QStringLiteral("Expected a column name, found %1").arg(describe(peek())));
                     return false;
                  }
                  endpoint.columns << next().text;
                  if (isSymbol(","))
                  {
                     next();
                     continue;
                  }
                  if (isSymbol(")"))
                  {
                     next();
                     break;
                  }
                  error(peek(), QStringLiteral("Expected ',' or ')' after the column name, found %1").arg(describe(peek())));
                  return false;
               }
               composite = true;
               break;
            }
            if (!isName(peek()))
            {
               error(peek(), QStringLiteral("Expected table.column, found %1").arg(describe(peek())));
               return false;
            }
            parts << next().text;
            if (isSymbol("."))
            {
               next();
               continue;
            }
            break;
         }

         if (!composite)
         {
            if (parts.size() < 2)
            {
               error(peek(-1), QStringLiteral("Expected table.column, found '%1'").arg(parts.join('.')));
               return false;
            }
            endpoint.columns = QStringList{parts.takeLast()};
         }
         if (parts.size() > 2)
         {
            error(peek(-1), QStringLiteral("Too many name parts in '%1'").arg(parts.join('.')));
            return false;
         }
         endpoint.table = parts.last();
         endpoint.schema = parts.size() == 2 ? parts.first() : QString();
         return true;
      }

      // endpoint relation endpoint [settings]
      bool parseRefBody(const QString& name, int line)
      {
         RefDTO ref;
         ref.name = name;
         ref.line = line;
         if (!parseEndpoint(ref.left) || !parseRelation(ref) || !parseEndpoint(ref.right))
         {
            return false;
         }
         if (isSymbol("["))
         {
            for (const Setting& setting : parseSettings())
            {
               if (setting.key == QLatin1String("delete") || setting.key == QLatin1String("update"))
               {
                  const QString action = settingText(setting).toLower();
                  static const QStringList actions{"cascade", "restrict", "set null", "set default", "no action"};
                  if (!actions.contains(action))
                  {
                     error(settingToken(setting), QStringLiteral("Expected cascade, restrict, set null, set default or no action"));
                  }
                  (setting.key == QLatin1String("delete") ? ref.onDelete : ref.onUpdate) = action;
               }
               else if (setting.key == QLatin1String("color"))
               {
                  settingColor(setting, ref.color);
               }
               else
               {
                  unknownSetting(setting, QStringLiteral("ref"));
               }
            }
         }
         m_schema.refs << ref;
         return true;
      }

      bool parseRefStatement()
      {
         const DbmlToken keyword = next();
         QString name;
         if (isName(peek()) && (isSymbol(":", 1) || isSymbol("{", 1)))
         {
            name = next().text;
         }
         if (isSymbol(":"))
         {
            next();
            if (!parseRefBody(name, keyword.line))
            {
               return false;
            }
            expectLineEnd();
            return true;
         }
         skipNewlines();
         if (!isSymbol("{"))
         {
            error(peek(), QStringLiteral("Expected ':' or '{' after Ref, found %1").arg(describe(peek())));
            return false;
         }
         next();
         while (true)
         {
            skipNewlines();
            if (isSymbol("}"))
            {
               next();
               return true;
            }
            if (peek().kind == DbmlToken::End)
            {
               error(keyword, QStringLiteral("Ref is not closed with '}'"));
               return true;
            }
            if (parseRefBody(name, peek().line))
            {
               expectLineEnd();
            }
            else
            {
               skipLine();
            }
         }
      }

      bool parseEnum()
      {
         const DbmlToken keyword = next();
         EnumDTO enumDTO;
         enumDTO.line = keyword.line;
         if (!parseQualifiedName(enumDTO.schema, enumDTO.name, QStringLiteral("enum")))
         {
            return false;
         }
         skipNewlines();
         if (!expectSymbol("{", QStringLiteral("to open the enum body")))
         {
            return false;
         }
         QSet<QString> names;
         while (true)
         {
            skipNewlines();
            if (isSymbol("}"))
            {
               next();
               break;
            }
            if (peek().kind == DbmlToken::End)
            {
               error(keyword, QStringLiteral("Enum '%1' is not closed with '}'").arg(enumDTO.name));
               break;
            }
            if (!isName(peek()) && peek().kind != DbmlToken::String)
            {
               error(peek(), QStringLiteral("Expected an enum value, found %1").arg(describe(peek())));
               skipLine();
               continue;
            }
            const DbmlToken valueToken = next();
            EnumValueDTO value{valueToken.text, QString()};
            if (names.contains(value.name))
            {
               error(valueToken, QStringLiteral("Enum value '%1' is already defined").arg(value.name));
            }
            names.insert(value.name);
            if (isSymbol("["))
            {
               for (const Setting& setting : parseSettings())
               {
                  if (setting.key == QLatin1String("note"))
                  {
                     settingString(setting, value.note);
                  }
                  else
                  {
                     unknownSetting(setting, QStringLiteral("enum value"));
                  }
               }
            }
            expectLineEnd();
            enumDTO.values << value;
         }

         for (const EnumDTO& existing : qAsConst(m_schema.enums))
         {
            if (existing.fullName() == enumDTO.fullName())
            {
               error(keyword, QStringLiteral("Enum '%1' is already defined").arg(enumDTO.name));
            }
         }
         m_schema.enums << enumDTO;
         return true;
      }

      bool parseTableGroup()
      {
         const DbmlToken keyword = next();
         TableGroupDTO group;
         group.line = keyword.line;
         if (!isName(peek()))
         {
            error(peek(), QStringLiteral("Expected a table group name, found %1").arg(describe(peek())));
            return false;
         }
         group.name = next().text;
         if (isSymbol("["))
         {
            for (const Setting& setting : parseSettings())
            {
               if (setting.key == QLatin1String("color"))
               {
                  settingColor(setting, group.color);
               }
               else if (setting.key == QLatin1String("note"))
               {
                  settingString(setting, group.note);
               }
               else
               {
                  unknownSetting(setting, QStringLiteral("table group"));
               }
            }
         }
         skipNewlines();
         if (!expectSymbol("{", QStringLiteral("to open the table group")))
         {
            return false;
         }
         const int groupIndex = m_schema.tableGroups.size();
         while (true)
         {
            skipNewlines();
            if (isSymbol("}"))
            {
               next();
               break;
            }
            if (peek().kind == DbmlToken::End)
            {
               error(keyword, QStringLiteral("Table group '%1' is not closed with '}'").arg(group.name));
               break;
            }
            if (isNoteLine())
            {
               if (parseNoteValue(group.note))
               {
                  expectLineEnd();
               }
               else
               {
                  skipLine();
               }
               continue;
            }
            PendingGroupTable member;
            member.group = groupIndex;
            member.token = peek();
            if (!parseQualifiedName(member.schema, member.name, QStringLiteral("table")))
            {
               skipLine();
               continue;
            }
            m_groupTables << member;
            expectLineEnd();
         }
         m_schema.tableGroups << group;
         return true;
      }

      bool parseProject()
      {
         const DbmlToken keyword = next();
         ProjectDTO& project = m_schema.project;
         if (project.defined)
         {
            error(keyword, QStringLiteral("Project is already defined"));
         }
         project.defined = true;
         project.line = keyword.line;
         if (isName(peek()))
         {
            project.name = next().text;
         }
         skipNewlines();
         if (!expectSymbol("{", QStringLiteral("to open the project")))
         {
            return false;
         }
         while (true)
         {
            skipNewlines();
            if (isSymbol("}"))
            {
               next();
               return true;
            }
            if (peek().kind == DbmlToken::End)
            {
               error(keyword, QStringLiteral("Project is not closed with '}'"));
               return true;
            }
            if (isNoteLine())
            {
               if (parseNoteValue(project.note))
               {
                  expectLineEnd();
               }
               else
               {
                  skipLine();
               }
               continue;
            }
            if (!isName(peek()) || !isSymbol(":", 1))
            {
               error(peek(), QStringLiteral("Expected key: 'value' in the project, found %1").arg(describe(peek())));
               skipLine();
               continue;
            }
            const QString key = next().text;
            next(); // :
            if (peek().kind != DbmlToken::String)
            {
               error(peek(), QStringLiteral("Expected a string value for '%1'").arg(key));
               skipLine();
               continue;
            }
            const QString value = next().text;
            if (key.compare(QLatin1String("database_type"), Qt::CaseInsensitive) == 0)
            {
               project.databaseType = value;
            }
            else
            {
               project.settings.insert(key, value);
            }
            expectLineEnd();
         }
      }

      bool parseStickyNote()
      {
         const DbmlToken keyword = next();
         NoteDTO note;
         note.line = keyword.line;
         if (!isName(peek()))
         {
            error(peek(), QStringLiteral("Expected a sticky note name, found %1").arg(describe(peek())));
            return false;
         }
         note.name = next().text;
         if (isSymbol("["))
         {
            for (const Setting& setting : parseSettings())
            {
               if (setting.key == QLatin1String("headercolor"))
               {
                  settingColor(setting, note.headerColor);
               }
               else
               {
                  unknownSetting(setting, QStringLiteral("note"));
               }
            }
         }
         skipNewlines();
         if (!expectSymbol("{", QStringLiteral("to open the note")))
         {
            return false;
         }
         skipNewlines();
         if (peek().kind != DbmlToken::String)
         {
            error(peek(), QStringLiteral("Expected the note text in single quotes, found %1").arg(describe(peek())));
            return false;
         }
         note.content = next().text;
         skipNewlines();
         if (!expectSymbol("}", QStringLiteral("to close the note")))
         {
            return false;
         }
         for (const NoteDTO& existing : qAsConst(m_schema.notes))
         {
            if (existing.name == note.name)
            {
               error(keyword, QStringLiteral("Note '%1' is already defined").arg(note.name));
            }
         }
         m_schema.notes << note;
         return true;
      }

      // ---- resolution ----

      void resolvePartials()
      {
         for (PendingTable& pending : m_tables)
         {
            if (pending.injections.isEmpty())
            {
               m_schema.tables << pending.table;
               continue;
            }

            TableDTO& table = pending.table;
            const QVector<ColumnDTO> own = table.columns;
            QSet<QString> ownNames;
            for (const ColumnDTO& column : own)
            {
               ownNames.insert(column.name);
            }

            QVector<ColumnDTO> columns;
            int ownIndex = 0;
            for (const Injection& injection : qAsConst(pending.injections))
            {
               while (ownIndex < injection.ownColumnsBefore)
               {
                  columns << own[ownIndex++];
               }
               if (!m_partials.contains(injection.partial))
               {
                  error(injection.token, QStringLiteral("Table partial '%1' is not defined").arg(injection.partial));
                  continue;
               }
               const Partial& partial = m_partials[injection.partial];
               for (const ColumnDTO& column : partial.table.columns)
               {
                  if (ownNames.contains(column.name))
                  {
                     continue;
                  }
                  auto existing = std::find_if(columns.begin(), columns.end(), [&](const ColumnDTO& c) {return c.name == column.name;});
                  if (existing != columns.end())
                  {
                     *existing = column;
                  }
                  else
                  {
                     columns << column;
                  }
               }
               for (RefDTO ref : partial.inlineRefs)
               {
                  if (ownNames.contains(ref.left.columns.value(0)))
                  {
                     continue;
                  }
                  ref.left.schema = table.schema;
                  ref.left.table = table.name;
                  m_schema.refs << ref;
               }
               table.indexes << partial.table.indexes;
               if (!pending.ownHeaderColor && !partial.table.headerColor.isEmpty())
               {
                  table.headerColor = partial.table.headerColor;
               }
               if (!pending.ownNote && !partial.table.note.isEmpty())
               {
                  table.note = partial.table.note;
               }
            }
            while (ownIndex < own.size())
            {
               columns << own[ownIndex++];
            }
            table.columns = columns;
            m_schema.tables << table;
         }
      }

      void validateTables()
      {
         QSet<QString> fullNames;
         for (const TableDTO& table : qAsConst(m_schema.tables))
         {
            if (fullNames.contains(table.fullName()))
            {
               errorAtLine(table.line, QStringLiteral("Table '%1' is already defined").arg(table.displayName()));
            }
            fullNames.insert(table.fullName());

            if (!table.alias.isEmpty())
            {
               if (m_aliases.contains(table.alias))
               {
                  errorAtLine(table.line, QStringLiteral("Alias '%1' is already used").arg(table.alias));
               }
               m_aliases.insert(table.alias, table.fullName());
            }

            QSet<QString> columnNames;
            for (const ColumnDTO& column : table.columns)
            {
               if (columnNames.contains(column.name))
               {
                  errorAtLine(column.line, QStringLiteral("Column '%1' is already defined in table '%2'").arg(column.name, table.displayName()));
               }
               columnNames.insert(column.name);
            }

            for (const IndexDTO& index : table.indexes)
            {
               for (const IndexColumnDTO& indexColumn : index.columns)
               {
                  if (!indexColumn.isExpression && !columnNames.contains(indexColumn.value))
                  {
                     errorAtLine(index.line, QStringLiteral("Column '%1' of the index is not in table '%2'").arg(indexColumn.value, table.displayName()));
                  }
               }
            }
         }
      }

      const TableDTO* findTable(const QString& schema, const QString& name) const
      {
         if (const TableDTO* table = m_schema.table(qualifiedName(schema, name)))
         {
            return table;
         }
         if (schema.isEmpty() && m_aliases.contains(name))
         {
            return m_schema.table(m_aliases[name]);
         }
         return nullptr;
      }

      bool resolveEndpoint(RefEndpointDTO& endpoint, int line)
      {
         const TableDTO* table = findTable(endpoint.schema, endpoint.table);
         const QString written = endpoint.schema.isEmpty() ? endpoint.table : endpoint.schema + '.' + endpoint.table;
         if (!table)
         {
            errorAtLine(line, QStringLiteral("Table '%1' is not defined").arg(written));
            return false;
         }
         bool ok = true;
         for (const QString& column : qAsConst(endpoint.columns))
         {
            if (table->columnIndex(column) < 0)
            {
               errorAtLine(line, QStringLiteral("Column '%1' is not in table '%2'").arg(column, written));
               ok = false;
            }
         }
         endpoint.schema = table->schema;
         endpoint.table = table->name;
         return ok;
      }

      void resolveRefs()
      {
         QVector<RefDTO> resolved;
         for (RefDTO ref : qAsConst(m_schema.refs))
         {
            const bool leftOk = resolveEndpoint(ref.left, ref.line);
            const bool rightOk = resolveEndpoint(ref.right, ref.line);
            if (!leftOk || !rightOk)
            {
               continue;
            }
            if (ref.left.columns.size() != ref.right.columns.size())
            {
               errorAtLine(ref.line, QStringLiteral("Both sides of a ref must have the same number of columns"));
               continue;
            }
            resolved << ref;
         }
         m_schema.refs = resolved;
      }

      void resolveGroups()
      {
         QHash<QString, QString> groupOfTable;
         for (const PendingGroupTable& member : qAsConst(m_groupTables))
         {
            TableGroupDTO& group = m_schema.tableGroups[member.group];
            const TableDTO* table = findTable(member.schema, member.name);
            if (!table)
            {
               error(member.token, QStringLiteral("Table '%1' is not defined").arg(member.name));
               continue;
            }
            if (groupOfTable.contains(table->fullName()))
            {
               error(member.token, QStringLiteral("Table '%1' is already in group '%2'").arg(table->displayName(), groupOfTable[table->fullName()]));
               continue;
            }
            groupOfTable.insert(table->fullName(), group.name);
            group.tables << table->fullName();
         }
      }

      const QString& m_source;
      DbmlParseResult& m_result;
      SchemaDTO& m_schema;
      QVector<DbmlToken> m_tokens;
      int m_pos = 0;

      QVector<PendingTable> m_tables;
      QHash<QString, Partial> m_partials;
      QHash<QString, QString> m_aliases; // alias -> full name
      QVector<PendingGroupTable> m_groupTables;
   };
}

DbmlParseResult DbmlParser::parse(const QString &source)
{
   DbmlParseResult result;
   Parser(source, result).run();
   return result;
}
