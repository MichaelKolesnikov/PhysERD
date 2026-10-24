#include "SqlImporter.h"
#include <QSet>
#include <QHash>

namespace
{
   struct SqlToken
   {
      enum Kind
      {
         Word,
         Quoted, // "name", `name`, [name]
         String,
         Number,
         Symbol,
         End
      };

      Kind kind = End;
      QString text;
      QString upper; // for words
      int line = 1;
   };

   using Statement = QVector<SqlToken>;

   QVector<Statement> tokenize(const QString& sql)
   {
      QVector<Statement> statements;
      Statement current;
      int line = 1;
      int pos = 0;
      int depth = 0;
      auto add = [&](SqlToken::Kind kind, const QString& text) {
         SqlToken token;
         token.kind = kind;
         token.text = text;
         token.upper = kind == SqlToken::Word ? text.toUpper() : text;
         token.line = line;
         current << token;
      };
      auto readQuoted = [&](QChar close) {
         QString value;
         ++pos;
         while (pos < sql.size())
         {
            if (sql[pos] == close)
            {
               if (pos + 1 < sql.size() && sql[pos + 1] == close)
               {
                  value += close;
                  pos += 2;
                  continue;
               }
               ++pos;
               break;
            }
            if (sql[pos] == '\n')
            {
               ++line;
            }
            if (close == '\'' && sql[pos] == '\\' && pos + 1 < sql.size())
            {
               value += sql[pos + 1];
               pos += 2;
               continue;
            }
            value += sql[pos++];
         }
         return value;
      };

      while (pos < sql.size())
      {
         const QChar c = sql[pos];
         const QChar next = pos + 1 < sql.size() ? sql[pos + 1] : QChar();
         if (c == '\n')
         {
            ++line;
            ++pos;
         }
         else if (c.isSpace())
         {
            ++pos;
         }
         else if ((c == '-' && next == '-') || c == '#')
         {
            while (pos < sql.size() && sql[pos] != '\n')
            {
               ++pos;
            }
         }
         else if (c == '/' && next == '*')
         {
            pos += 2;
            while (pos < sql.size() && !(sql[pos] == '*' && pos + 1 < sql.size() && sql[pos + 1] == '/'))
            {
               if (sql[pos] == '\n')
               {
                  ++line;
               }
               ++pos;
            }
            pos += 2;
         }
         else if (c == '\'')
         {
            add(SqlToken::String, readQuoted('\''));
         }
         else if ((c == 'N' || c == 'E' || c == 'n' || c == 'e') && next == '\'')
         {
            ++pos;
            add(SqlToken::String, readQuoted('\''));
         }
         else if (c == '"' || c == '`')
         {
            add(SqlToken::Quoted, readQuoted(c));
         }
         else if (c == '[' && next != ']')
         {
            add(SqlToken::Quoted, readQuoted(']'));
         }
         else if (c.isDigit())
         {
            const int start = pos;
            while (pos < sql.size() && (sql[pos].isDigit() || sql[pos] == '.'))
            {
               ++pos;
            }
            add(SqlToken::Number, sql.mid(start, pos - start));
         }
         else if (c.isLetter() || c == '_' || c == '@')
         {
            const int start = pos;
            while (pos < sql.size() && (sql[pos].isLetterOrNumber() || sql[pos] == '_' || sql[pos] == '$' || sql[pos] == '@'))
            {
               ++pos;
            }
            add(SqlToken::Word, sql.mid(start, pos - start));
         }
         else if (c == ':' && next == ':')
         {
            pos += 2;
            add(SqlToken::Symbol, QStringLiteral("::"));
         }
         else if (c == ';' && depth == 0)
         {
            ++pos;
            if (!current.isEmpty())
            {
               statements << current;
               current.clear();
            }
         }
         else
         {
            if (c == '(')
            {
               ++depth;
            }
            else if (c == ')')
            {
               depth = std::max(0, depth - 1);
            }
            ++pos;
            add(SqlToken::Symbol, QString(c));
         }
      }
      // GO separates SQL Server batches like a semicolon
      if (!current.isEmpty())
      {
         statements << current;
      }
      QVector<Statement> split;
      for (const Statement& statement : qAsConst(statements))
      {
         Statement part;
         for (const SqlToken& token : statement)
         {
            if (token.kind == SqlToken::Word && token.upper == QLatin1String("GO"))
            {
               if (!part.isEmpty())
               {
                  split << part;
               }
               part.clear();
               continue;
            }
            part << token;
         }
         if (!part.isEmpty())
         {
            split << part;
         }
      }
      return split;
   }

   struct PendingRef
   {
      QString fromSchema;
      QString fromTable;
      QStringList fromColumns;
      QString toSchema;
      QString toTable;
      QStringList toColumns;
      QString name;
      QString onDelete;
      QString onUpdate;
      int line = 0;
   };

   class Importer
   {
   public:
      SqlImportResult run(const QString& sql)
      {
         for (const Statement& statement : tokenize(sql))
         {
            m_tokens = statement;
            SqlToken end;
            end.line = statement.isEmpty() ? 0 : statement.last().line;
            m_tokens << end;
            m_pos = 0;
            parseStatement();
         }
         finishRefs();
         return m_result;
      }

   private:
      const SqlToken& peek(int offset = 0) const
      {
         return m_tokens[std::min(m_pos + offset, m_tokens.size() - 1)];
      }

      const SqlToken& next()
      {
         const SqlToken& token = m_tokens[std::min(m_pos, m_tokens.size() - 1)];
         m_pos = std::min(m_pos + 1, m_tokens.size() - 1);
         return token;
      }

      bool isWord(const char* word, int offset = 0) const
      {
         const SqlToken& token = peek(offset);
         return token.kind == SqlToken::Word && token.upper == QLatin1String(word);
      }

      bool acceptWord(const char* word)
      {
         if (isWord(word))
         {
            next();
            return true;
         }
         return false;
      }

      bool isSymbol(const char* symbol, int offset = 0) const
      {
         const SqlToken& token = peek(offset);
         return token.kind == SqlToken::Symbol && token.text == QLatin1String(symbol);
      }

      bool atEnd() const
      {
         return peek().kind == SqlToken::End;
      }

      static bool isName(const SqlToken& token)
      {
         return token.kind == SqlToken::Word || token.kind == SqlToken::Quoted;
      }

      void warn(const QString& message)
      {
         m_result.warnings << QStringLiteral("Line %1: %2").arg(peek().line).arg(message);
      }

      // a, schema.a, db.schema.a; "public" and "dbo" are the default schemas
      bool parseName(QString& schema, QString& name)
      {
         if (!isName(peek()))
         {
            return false;
         }
         QStringList parts{next().text};
         while (isSymbol(".") && isName(peek(1)))
         {
            next();
            parts << next().text;
         }
         name = parts.last();
         schema = parts.size() >= 2 ? parts[parts.size() - 2] : QString();
         if (schema.compare(QLatin1String("public"), Qt::CaseInsensitive) == 0 || schema.compare(QLatin1String("dbo"), Qt::CaseInsensitive) == 0)
         {
            schema.clear();
         }
         return true;
      }

      // Skips a balanced (...) group, the current token is '('
      void skipGroup()
      {
         int depth = 0;
         while (!atEnd())
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
                  next();
                  return;
               }
            }
            next();
         }
      }

      // (a, b DESC, c(10)) -> a, b, c
      QStringList parseColumnList()
      {
         QStringList columns;
         if (!isSymbol("("))
         {
            return columns;
         }
         next();
         bool expectName = true;
         int depth = 0;
         while (!atEnd())
         {
            if (depth == 0 && isSymbol(")"))
            {
               next();
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
            else if (depth == 0 && isSymbol(","))
            {
               expectName = true;
               next();
               continue;
            }
            else if (depth == 0 && expectName && isName(peek()))
            {
               columns << peek().text;
               expectName = false;
            }
            next();
         }
         return columns;
      }

      void parseStatement()
      {
         if (isWord("CREATE"))
         {
            next();
            acceptWord("OR");
            acceptWord("REPLACE");
            while (isWord("TEMP") || isWord("TEMPORARY") || isWord("UNLOGGED") || isWord("GLOBAL") || isWord("LOCAL"))
            {
               next();
            }
            if (acceptWord("TABLE"))
            {
               parseCreateTable();
               return;
            }
            if (acceptWord("TYPE"))
            {
               parseCreateType();
               return;
            }
            const bool unique = acceptWord("UNIQUE");
            acceptWord("CLUSTERED");
            acceptWord("NONCLUSTERED");
            if (acceptWord("INDEX"))
            {
               parseCreateIndex(unique);
               return;
            }
         }
         else if (isWord("ALTER") && isWord("TABLE", 1))
         {
            next();
            next();
            parseAlterTable();
            return;
         }
         else if (isWord("COMMENT") && isWord("ON", 1))
         {
            next();
            next();
            parseComment();
            return;
         }

         static const QSet<QString> silent{"SET", "USE", "BEGIN", "COMMIT", "START", "DROP", "INSERT", "LOCK", "UNLOCK", "SELECT", "GRANT", "REVOKE"};
         if (!silent.contains(peek().upper))
         {
            warn(QStringLiteral("skipped statement starting with '%1'").arg(peek().text));
         }
      }

      void parseCreateTable()
      {
         if (isWord("IF") && isWord("NOT", 1) && isWord("EXISTS", 2))
         {
            m_pos += 3;
         }
         TableDTO table;
         table.line = peek().line;
         if (!parseName(table.schema, table.name))
         {
            warn(QStringLiteral("CREATE TABLE without a name"));
            return;
         }
         if (!isSymbol("("))
         {
            warn(QStringLiteral("CREATE TABLE %1 has no column list (CREATE TABLE ... AS is not supported)").arg(table.name));
            return;
         }
         next();

         while (!atEnd() && !isSymbol(")"))
         {
            parseTableElement(table);
            if (isSymbol(","))
            {
               next();
            }
         }
         next(); // )

         // MySQL table options: COMMENT='...'
         while (!atEnd())
         {
            if (acceptWord("COMMENT"))
            {
               if (isSymbol("="))
               {
                  next();
               }
               if (peek().kind == SqlToken::String)
               {
                  table.note = next().text;
               }
               continue;
            }
            next();
         }

         const QString fullName = table.fullName();
         if (m_result.schema.tableIndex(fullName) >= 0)
         {
            warn(QStringLiteral("table %1 is defined twice, the second definition is skipped").arg(table.name));
            return;
         }
         m_result.schema.tables << table;
      }

      // Skips to the ',' or ')' that ends a table element
      void skipElement()
      {
         int depth = 0;
         while (!atEnd())
         {
            if (depth == 0 && (isSymbol(",") || isSymbol(")")))
            {
               return;
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
      }

      void parseTableElement(TableDTO& table)
      {
         QString constraintName;
         if (isWord("CONSTRAINT"))
         {
            next();
            if (isName(peek()))
            {
               constraintName = next().text;
            }
         }

         if (isWord("PRIMARY") && isWord("KEY", 1))
         {
            m_pos += 2;
            acceptWord("CLUSTERED");
            acceptWord("NONCLUSTERED");
            const QStringList columns = parseColumnList();
            setPrimaryKey(table, columns);
            skipElement();
            return;
         }
         if (isWord("UNIQUE") || ((isWord("KEY") || isWord("INDEX")) && isIndexDefinition()) || isWord("FULLTEXT") || isWord("SPATIAL"))
         {
            const bool unique = acceptWord("UNIQUE");
            acceptWord("FULLTEXT");
            acceptWord("SPATIAL");
            if (!acceptWord("KEY"))
            {
               acceptWord("INDEX");
            }
            QString name = constraintName;
            if (isName(peek()) && !isSymbol("(", 0))
            {
               name = next().text;
            }
            const QStringList columns = parseColumnList();
            if (unique && columns.size() == 1 && table.columnIndex(columns.first()) >= 0)
            {
               table.columns[table.columnIndex(columns.first())].unique = true;
            }
            else if (!columns.isEmpty())
            {
               IndexDTO index;
               index.unique = unique;
               index.name = name;
               for (const QString& column : columns)
               {
                  index.columns << IndexColumnDTO{column, false};
               }
               table.indexes << index;
            }
            skipElement();
            return;
         }
         if (isWord("FOREIGN") && isWord("KEY", 1))
         {
            m_pos += 2;
            PendingRef ref;
            ref.line = peek().line;
            ref.name = constraintName;
            ref.fromSchema = table.schema;
            ref.fromTable = table.name;
            ref.fromColumns = parseColumnList();
            if (acceptWord("REFERENCES"))
            {
               parseReferences(ref);
            }
            skipElement();
            return;
         }
         if (isWord("CHECK") || isWord("EXCLUDE"))
         {
            skipElement();
            return;
         }
         if (!constraintName.isEmpty())
         {
            skipElement();
            return;
         }
         parseColumn(table);
      }

      // KEY idx (a) and INDEX (a) define indexes, but "key varchar(10)" is a column named key
      bool isIndexDefinition() const
      {
         return isSymbol("(", 1) || (isName(peek(1)) && isSymbol("(", 2) && isName(peek(3)));
      }

      void setPrimaryKey(TableDTO& table, const QStringList& columns)
      {
         if (columns.size() == 1 && table.columnIndex(columns.first()) >= 0)
         {
            table.columns[table.columnIndex(columns.first())].pk = true;
            return;
         }
         if (columns.isEmpty())
         {
            return;
         }
         IndexDTO index;
         index.pk = true;
         for (const QString& column : columns)
         {
            index.columns << IndexColumnDTO{column, false};
         }
         table.indexes << index;
      }

      bool isConstraintStart() const
      {
         static const QSet<QString> keywords{
            "NOT", "NULL", "PRIMARY", "UNIQUE", "DEFAULT", "AUTO_INCREMENT", "AUTOINCREMENT", "IDENTITY", "REFERENCES",
            "CHECK", "COMMENT", "CONSTRAINT", "GENERATED", "COLLATE", "CHARSET", "ON", "KEY"
         };
         const SqlToken& token = peek();
         if (token.kind != SqlToken::Word)
         {
            return false;
         }
         if (token.upper == QLatin1String("CHARACTER") && isWord("SET", 1))
         {
            return true;
         }
         return keywords.contains(token.upper);
      }

      void parseColumn(TableDTO& table)
      {
         if (!isName(peek()))
         {
            warn(QStringLiteral("unexpected '%1' in table %2").arg(peek().text, table.name));
            skipElement();
            return;
         }
         ColumnDTO column;
         column.line = peek().line;
         column.name = next().text;

         // type: words and (arguments) up to the first constraint
         QString type;
         QStringList enumValues;
         while (!atEnd() && !isSymbol(",") && !isSymbol(")") && !isConstraintStart())
         {
            const SqlToken& token = peek();
            if (token.kind == SqlToken::Symbol && token.text == '(')
            {
               if (type.compare(QLatin1String("enum"), Qt::CaseInsensitive) == 0 || type.compare(QLatin1String("set"), Qt::CaseInsensitive) == 0)
               {
                  next();
                  while (!atEnd() && !isSymbol(")"))
                  {
                     if (peek().kind == SqlToken::String)
                     {
                        enumValues << peek().text;
                     }
                     next();
                  }
                  next();
                  continue;
               }
               QString arguments;
               int depth = 0;
               while (!atEnd())
               {
                  if (isSymbol("("))
                  {
                     ++depth;
                  }
                  else if (isSymbol(")"))
                  {
                     --depth;
                  }
                  arguments += peek().kind == SqlToken::String ? '\'' + peek().text + '\'' : peek().text;
                  next();
                  if (depth == 0)
                  {
                     break;
                  }
               }
               type += arguments;
               continue;
            }
            if (token.kind == SqlToken::Symbol && token.text == '[' && isSymbol("]", 1))
            {
               next();
               next();
               type += QStringLiteral("[]");
               continue;
            }
            if (!type.isEmpty() && !type.endsWith('.') && token.text != '.')
            {
               type += ' ';
            }
            type += token.kind == SqlToken::Word ? token.text.toLower() : token.text;
            next();
         }

         if (type == QLatin1String("serial") || type == QLatin1String("serial4"))
         {
            type = QStringLiteral("integer");
            column.increment = true;
         }
         else if (type == QLatin1String("bigserial") || type == QLatin1String("serial8"))
         {
            type = QStringLiteral("bigint");
            column.increment = true;
         }
         else if (type == QLatin1String("smallserial") || type == QLatin1String("serial2"))
         {
            type = QStringLiteral("smallint");
            column.increment = true;
         }
         if (!enumValues.isEmpty())
         {
            EnumDTO enumDTO;
            enumDTO.schema = table.schema;
            enumDTO.name = table.name + '_' + column.name + QStringLiteral("_enum");
            for (const QString& value : qAsConst(enumValues))
            {
               enumDTO.values << EnumValueDTO{value, QString()};
            }
            m_result.schema.enums << enumDTO;
            type = enumDTO.schema.isEmpty() ? enumDTO.name : enumDTO.schema + '.' + enumDTO.name;
         }
         if (type.isEmpty())
         {
            type = QStringLiteral("unknown");
         }
         column.type = type;

         parseColumnConstraints(table, column);
         table.columns << column;
      }

      void parseColumnConstraints(const TableDTO& table, ColumnDTO& column)
      {
         while (!atEnd() && !isSymbol(",") && !isSymbol(")"))
         {
            if (isWord("NOT") && isWord("NULL", 1))
            {
               m_pos += 2;
               column.notNull = true;
            }
            else if (acceptWord("NULL"))
            {
               column.notNull = false;
            }
            else if (isWord("PRIMARY") && isWord("KEY", 1))
            {
               m_pos += 2;
               column.pk = true;
            }
            else if (acceptWord("UNIQUE"))
            {
               acceptWord("KEY");
               column.unique = true;
            }
            else if (acceptWord("DEFAULT"))
            {
               parseDefault(column);
               // PostgreSQL dumps write serial columns as DEFAULT nextval('sequence'::regclass)
               if (column.defaultKind == ColumnDTO::ExpressionDefault && column.defaultValue.startsWith(QLatin1String("nextval("), Qt::CaseInsensitive))
               {
                  column.increment = true;
                  column.defaultKind = ColumnDTO::NoDefault;
                  column.defaultValue.clear();
               }
            }
            else if (acceptWord("AUTO_INCREMENT") || acceptWord("AUTOINCREMENT"))
            {
               column.increment = true;
            }
            else if (acceptWord("IDENTITY"))
            {
               column.increment = true;
               if (isSymbol("("))
               {
                  skipGroup();
               }
            }
            else if (acceptWord("GENERATED"))
            {
               bool identity = false;
               while (!atEnd() && !isSymbol(",") && !isSymbol(")") && !isSymbol("("))
               {
                  identity = identity || isWord("IDENTITY");
                  const bool stop = isWord("IDENTITY");
                  next();
                  if (stop)
                  {
                     break;
                  }
               }
               if (isSymbol("("))
               {
                  skipGroup();
               }
               acceptWord("STORED");
               acceptWord("VIRTUAL");
               column.increment = column.increment || identity;
            }
            else if (acceptWord("REFERENCES"))
            {
               PendingRef ref;
               ref.line = column.line;
               ref.fromSchema = table.schema;
               ref.fromTable = table.name;
               ref.fromColumns = QStringList{column.name};
               parseReferences(ref);
            }
            else if (acceptWord("CHECK"))
            {
               if (isSymbol("("))
               {
                  skipGroup();
               }
            }
            else if (acceptWord("COMMENT"))
            {
               if (peek().kind == SqlToken::String)
               {
                  column.note = next().text;
               }
            }
            else if (acceptWord("CONSTRAINT"))
            {
               if (isName(peek()))
               {
                  next();
               }
            }
            else if (acceptWord("COLLATE") || acceptWord("CHARSET"))
            {
               next();
            }
            else if (isWord("CHARACTER") && isWord("SET", 1))
            {
               m_pos += 3;
            }
            else if (isWord("ON") && isWord("UPDATE", 1))
            {
               m_pos += 2;
               parseExpressionText();
            }
            else
            {
               next();
            }
         }
      }

      // Tokens up to the next constraint or the end of the column, as SQL text
      QString parseExpressionText()
      {
         QString text;
         int depth = 0;
         while (!atEnd())
         {
            if (depth == 0 && (isSymbol(",") || isSymbol(")") || (isConstraintStart() && !text.isEmpty())))
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
            const SqlToken& token = peek();
            const bool word = token.kind == SqlToken::Word || token.kind == SqlToken::Number;
            if (!text.isEmpty() && word && (text.back().isLetterOrNumber() || text.back() == ')'))
            {
               text += ' ';
            }
            text += token.kind == SqlToken::String ? '\'' + QString(token.text).replace('\'', QStringLiteral("''")) + '\'' : token.text;
            next();
         }
         return text;
      }

      void parseDefault(ColumnDTO& column)
      {
         // ('value'), (0)
         int wrapped = 0;
         while (isSymbol("(") && (peek(1).kind == SqlToken::String || peek(1).kind == SqlToken::Number || isSymbol("(", 1) || isSymbol("-", 1)))
         {
            next();
            ++wrapped;
         }
         const SqlToken& first = peek();
         if (first.kind == SqlToken::String)
         {
            column.defaultKind = ColumnDTO::StringDefault;
            column.defaultValue = next().text;
            if (isSymbol("::"))
            {
               // 'value'::character varying
               next();
               while ((isName(peek()) && !isConstraintStart()) || isSymbol("(") || isSymbol("[") || isSymbol("]"))
               {
                  if (isSymbol("("))
                  {
                     skipGroup();
                  }
                  else
                  {
                     next();
                  }
               }
            }
         }
         else if (first.kind == SqlToken::Number || (isSymbol("-") && peek(1).kind == SqlToken::Number))
         {
            QString number;
            if (isSymbol("-"))
            {
               number = '-';
               next();
            }
            number += next().text;
            column.defaultKind = ColumnDTO::NumberDefault;
            column.defaultValue = number;
         }
         else if (isWord("TRUE") || isWord("FALSE"))
         {
            column.defaultKind = ColumnDTO::BooleanDefault;
            column.defaultValue = next().text.toLower();
         }
         else if (isWord("NULL"))
         {
            next();
            column.defaultKind = ColumnDTO::NullDefault;
            column.defaultValue = QStringLiteral("null");
         }
         else
         {
            column.defaultKind = ColumnDTO::ExpressionDefault;
            column.defaultValue = parseExpressionText();
         }
         for (int i = 0; i < wrapped && isSymbol(")"); ++i)
         {
            next();
         }
      }

      void parseReferences(PendingRef& ref)
      {
         parseName(ref.toSchema, ref.toTable);
         ref.toColumns = parseColumnList();
         while (isWord("ON") || isWord("MATCH") || isWord("DEFERRABLE") || isWord("NOT") || isWord("INITIALLY"))
         {
            if (isWord("ON") && (isWord("DELETE", 1) || isWord("UPDATE", 1)))
            {
               const bool isDelete = isWord("DELETE", 1);
               m_pos += 2;
               QString action;
               if (acceptWord("CASCADE"))
               {
                  action = QStringLiteral("cascade");
               }
               else if (acceptWord("RESTRICT"))
               {
                  action = QStringLiteral("restrict");
               }
               else if (isWord("SET") && isWord("NULL", 1))
               {
                  m_pos += 2;
                  action = QStringLiteral("set null");
               }
               else if (isWord("SET") && isWord("DEFAULT", 1))
               {
                  m_pos += 2;
                  action = QStringLiteral("set default");
               }
               else if (isWord("NO") && isWord("ACTION", 1))
               {
                  m_pos += 2;
                  action = QStringLiteral("no action");
               }
               (isDelete ? ref.onDelete : ref.onUpdate) = action;
            }
            else if (isWord("NOT") && !isWord("DEFERRABLE", 1))
            {
               break;
            }
            else
            {
               next();
               if (isName(peek()) && !isWord("ON") && !isWord("NOT"))
               {
                  next();
               }
            }
         }
         m_refs << ref;
      }

      void parseCreateType()
      {
         EnumDTO enumDTO;
         enumDTO.line = peek().line;
         if (!parseName(enumDTO.schema, enumDTO.name))
         {
            return;
         }
         if (!acceptWord("AS") || !acceptWord("ENUM"))
         {
            warn(QStringLiteral("type %1 is not an enum, skipped").arg(enumDTO.name));
            return;
         }
         if (!isSymbol("("))
         {
            return;
         }
         next();
         while (!atEnd() && !isSymbol(")"))
         {
            if (peek().kind == SqlToken::String)
            {
               enumDTO.values << EnumValueDTO{peek().text, QString()};
            }
            next();
         }
         m_result.schema.enums << enumDTO;
      }

      TableDTO* findTable(const QString& schema, const QString& name)
      {
         for (TableDTO& table : m_result.schema.tables)
         {
            if (table.fullName() == qualifiedName(schema, name))
            {
               return &table;
            }
         }
         // names are case-insensitive in SQL unless quoted
         for (TableDTO& table : m_result.schema.tables)
         {
            if (table.schema.compare(schema, Qt::CaseInsensitive) == 0 && table.name.compare(name, Qt::CaseInsensitive) == 0)
            {
               return &table;
            }
         }
         return nullptr;
      }

      void parseCreateIndex(bool unique)
      {
         acceptWord("CONCURRENTLY");
         if (isWord("IF") && isWord("NOT", 1) && isWord("EXISTS", 2))
         {
            m_pos += 3;
         }
         IndexDTO index;
         index.unique = unique;
         index.line = peek().line;
         if (!isWord("ON"))
         {
            QString schema;
            parseName(schema, index.name);
         }
         if (!acceptWord("ON"))
         {
            warn(QStringLiteral("CREATE INDEX without ON, skipped"));
            return;
         }
         acceptWord("ONLY");
         QString schema;
         QString tableName;
         parseName(schema, tableName);
         if (acceptWord("USING"))
         {
            index.type = next().text.toLower();
         }
         if (!isSymbol("("))
         {
            return;
         }
         // columns or expressions
         next();
         int depth = 0;
         QString current;
         bool currentIsExpression = false;
         auto flush = [&]() {
            current = current.trimmed();
            if (!current.isEmpty())
            {
               index.columns << IndexColumnDTO{current, currentIsExpression};
            }
            current.clear();
            currentIsExpression = false;
         };
         while (!atEnd())
         {
            if (depth == 0 && isSymbol(")"))
            {
               next();
               break;
            }
            if (depth == 0 && isSymbol(","))
            {
               flush();
               next();
               continue;
            }
            const SqlToken& token = peek();
            if (isSymbol("("))
            {
               ++depth;
               currentIsExpression = true;
            }
            else if (isSymbol(")"))
            {
               --depth;
            }
            if (depth == 0 && (isWord("ASC") || isWord("DESC")) && !current.isEmpty())
            {
               next();
               continue;
            }
            if (!current.isEmpty() && token.kind == SqlToken::Word)
            {
               if (current.back().isLetterOrNumber() || current.back() == ')')
               {
                  current += ' ';
               }
               currentIsExpression = true;
            }
            current += token.kind == SqlToken::String ? '\'' + token.text + '\'' : token.text;
            next();
         }
         flush();
         if (acceptWord("USING"))
         {
            index.type = next().text.toLower();
         }
         if (index.type != QLatin1String("btree") && index.type != QLatin1String("hash"))
         {
            index.type.clear();
         }
         if (current.isEmpty() && index.columns.isEmpty())
         {
            return;
         }

         TableDTO* table = findTable(schema, tableName);
         if (!table)
         {
            warn(QStringLiteral("index on unknown table %1, skipped").arg(tableName));
            return;
         }
         for (IndexColumnDTO& column : index.columns)
         {
            if (column.isExpression && column.value.startsWith('(') && column.value.endsWith(')'))
            {
               column.value = column.value.mid(1, column.value.size() - 2);
            }
         }
         table->indexes << index;
      }

      void parseAlterTable()
      {
         acceptWord("ONLY");
         if (isWord("IF") && isWord("EXISTS", 1))
         {
            m_pos += 2;
         }
         QString schema;
         QString tableName;
         parseName(schema, tableName);
         acceptWord("ONLY");
         TableDTO* table = findTable(schema, tableName);
         if (!table)
         {
            warn(QStringLiteral("ALTER TABLE of unknown table %1, skipped").arg(tableName));
            return;
         }
         acceptWord("WITH");
         acceptWord("CHECK");
         acceptWord("NOCHECK");
         bool handled = false;
         while (acceptWord("ADD"))
         {
            QString constraintName;
            if (acceptWord("CONSTRAINT") && isName(peek()))
            {
               constraintName = next().text;
            }
            if (isWord("FOREIGN") && isWord("KEY", 1))
            {
               m_pos += 2;
               PendingRef ref;
               ref.line = peek().line;
               ref.name = constraintName;
               ref.fromSchema = table->schema;
               ref.fromTable = table->name;
               ref.fromColumns = parseColumnList();
               if (acceptWord("REFERENCES"))
               {
                  parseReferences(ref);
               }
               handled = true;
            }
            else if (isWord("PRIMARY") && isWord("KEY", 1))
            {
               m_pos += 2;
               acceptWord("CLUSTERED");
               acceptWord("NONCLUSTERED");
               setPrimaryKey(*table, parseColumnList());
               handled = true;
            }
            else if (acceptWord("UNIQUE"))
            {
               acceptWord("KEY");
               acceptWord("INDEX");
               acceptWord("CLUSTERED");
               acceptWord("NONCLUSTERED");
               const QStringList columns = parseColumnList();
               if (columns.size() == 1 && table->columnIndex(columns.first()) >= 0)
               {
                  table->columns[table->columnIndex(columns.first())].unique = true;
               }
               else if (!columns.isEmpty())
               {
                  IndexDTO index;
                  index.unique = true;
                  index.name = constraintName;
                  for (const QString& column : columns)
                  {
                     index.columns << IndexColumnDTO{column, false};
                  }
                  table->indexes << index;
               }
               handled = true;
            }
            // up to the next ADD
            int depth = 0;
            while (!atEnd() && !(depth == 0 && isSymbol(",")))
            {
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
            if (isSymbol(","))
            {
               next();
            }
         }
         if (!handled)
         {
            warn(QStringLiteral("ALTER TABLE %1: only ADD FOREIGN KEY / PRIMARY KEY / UNIQUE are imported").arg(tableName));
         }
      }

      void parseComment()
      {
         const bool onTable = acceptWord("TABLE");
         const bool onColumn = !onTable && acceptWord("COLUMN");
         if (!onTable && !onColumn)
         {
            return;
         }
         QStringList parts;
         while (isName(peek()))
         {
            parts << next().text;
            if (!isSymbol("."))
            {
               break;
            }
            next();
         }
         if (!acceptWord("IS") || peek().kind != SqlToken::String)
         {
            return;
         }
         const QString text = next().text;
         QString column;
         if (onColumn)
         {
            if (parts.size() < 2)
            {
               return;
            }
            column = parts.takeLast();
         }
         if (parts.isEmpty())
         {
            return;
         }
         QString schema = parts.size() >= 2 ? parts[parts.size() - 2] : QString();
         if (schema.compare(QLatin1String("public"), Qt::CaseInsensitive) == 0)
         {
            schema.clear();
         }
         TableDTO* table = findTable(schema, parts.last());
         if (!table)
         {
            return;
         }
         if (onTable)
         {
            table->note = text;
            return;
         }
         const int index = table->columnIndex(column);
         if (index >= 0)
         {
            table->columns[index].note = text;
         }
      }

      static bool isUniqueKey(const TableDTO& table, const QStringList& columns)
      {
         if (columns.size() == 1)
         {
            const int index = table.columnIndex(columns.first());
            if (index >= 0 && (table.columns[index].unique || table.columns[index].pk))
            {
               return true;
            }
         }
         for (const IndexDTO& index : table.indexes)
         {
            if (!index.pk && !index.unique)
            {
               continue;
            }
            QStringList indexColumns;
            for (const IndexColumnDTO& column : index.columns)
            {
               indexColumns << column.value;
            }
            if (indexColumns == columns)
            {
               return true;
            }
         }
         return false;
      }

      void finishRefs()
      {
         for (PendingRef& pending : m_refs)
         {
            TableDTO* from = findTable(pending.fromSchema, pending.fromTable);
            TableDTO* to = findTable(pending.toSchema, pending.toTable);
            if (!from || !to)
            {
               m_result.warnings << QStringLiteral("Line %1: foreign key to unknown table %2, skipped").arg(pending.line).arg(pending.toTable);
               continue;
            }
            if (pending.toColumns.isEmpty())
            {
               // REFERENCES table without columns targets its primary key
               for (const ColumnDTO& column : to->columns)
               {
                  if (column.pk)
                  {
                     pending.toColumns << column.name;
                  }
               }
            }
            if (pending.toColumns.size() != pending.fromColumns.size() || pending.fromColumns.isEmpty())
            {
               m_result.warnings << QStringLiteral("Line %1: foreign key from %2 has mismatching columns, skipped").arg(pending.line).arg(pending.fromTable);
               continue;
            }
            auto fixCase = [](const TableDTO& table, QStringList& columns) {
               for (QString& column : columns)
               {
                  for (const ColumnDTO& candidate : table.columns)
                  {
                     if (candidate.name.compare(column, Qt::CaseInsensitive) == 0)
                     {
                        column = candidate.name;
                     }
                  }
               }
            };
            fixCase(*from, pending.fromColumns);
            fixCase(*to, pending.toColumns);

            RefDTO ref;
            ref.name = pending.name;
            ref.left = RefEndpointDTO{from->schema, from->name, pending.fromColumns};
            ref.right = RefEndpointDTO{to->schema, to->name, pending.toColumns};
            ref.leftCardinality = isUniqueKey(*from, pending.fromColumns) ? Cardinality::One : Cardinality::Many;
            ref.rightCardinality = Cardinality::One;
            ref.onDelete = pending.onDelete;
            ref.onUpdate = pending.onUpdate;
            m_result.schema.refs << ref;
         }
      }

      SqlImportResult m_result;
      QVector<SqlToken> m_tokens;
      int m_pos = 0;
      QVector<PendingRef> m_refs;
   };
}

SqlImportResult SqlImporter::import(const QString &sql)
{
   return Importer().run(sql);
}
