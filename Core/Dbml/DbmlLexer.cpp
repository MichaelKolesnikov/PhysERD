#include "DbmlLexer.h"
#include <QStringList>
#include <algorithm>
#include <climits>

namespace
{
   bool isIdentifierStart(QChar c)
   {
      return c.isLetter() || c == '_';
   }

   bool isIdentifierPart(QChar c)
   {
      return c.isLetterOrNumber() || c == '_';
   }

   class Lexer
   {
   public:
      Lexer(const QString& source, QVector<DbmlError>* errors)
         : m_source(source), m_errors(errors)
      {

      }

      QVector<DbmlToken> run()
      {
         while (m_pos < m_source.size())
         {
            const QChar c = m_source[m_pos];
            if (c == '\n')
            {
               beginToken();
               advance();
               if (!m_tokens.isEmpty() && m_tokens.last().kind != DbmlToken::Newline)
               {
                  addToken(DbmlToken::Newline, QStringLiteral("\n"));
               }
            }
            else if (c.isSpace())
            {
               advance();
            }
            else if (c == '/' && peek(1) == '/')
            {
               while (m_pos < m_source.size() && m_source[m_pos] != '\n')
               {
                  advance();
               }
            }
            else if (c == '/' && peek(1) == '*')
            {
               skipBlockComment();
            }
            else if (c == '\'')
            {
               lexString();
            }
            else if (c == '"')
            {
               lexQuoted('"', DbmlToken::QuotedIdentifier, QStringLiteral("Unterminated quoted name"));
            }
            else if (c == '`')
            {
               lexQuoted('`', DbmlToken::Expression, QStringLiteral("Unterminated expression"));
            }
            else if (c == '#')
            {
               lexColor();
            }
            else if (c.isDigit())
            {
               lexNumberOrIdentifier();
            }
            else if (isIdentifierStart(c))
            {
               lexIdentifier();
            }
            else if (c == '<' && peek(1) == '>')
            {
               beginToken();
               advance(2);
               addToken(DbmlToken::Symbol, QStringLiteral("<>"));
            }
            else if (QStringLiteral("{}[](),:.<>-~").contains(c))
            {
               beginToken();
               advance();
               addToken(DbmlToken::Symbol, QString(c));
            }
            else
            {
               error(m_line, m_column, 1, QStringLiteral("Unexpected character '%1'").arg(c));
               advance();
            }
         }

         beginToken();
         addToken(DbmlToken::End, QString());
         return m_tokens;
      }

   private:
      QChar peek(int offset) const
      {
         const int index = m_pos + offset;
         return index < m_source.size() ? m_source[index] : QChar();
      }

      void advance(int count = 1)
      {
         for (int i = 0; i < count && m_pos < m_source.size(); ++i)
         {
            if (m_source[m_pos] == '\n')
            {
               ++m_line;
               m_column = 1;
            }
            else
            {
               ++m_column;
            }
            ++m_pos;
         }
      }

      void beginToken()
      {
         m_tokenPos = m_pos;
         m_tokenLine = m_line;
         m_tokenColumn = m_column;
      }

      void addToken(DbmlToken::Kind kind, const QString& text)
      {
         DbmlToken token;
         token.kind = kind;
         token.text = text;
         token.line = m_tokenLine;
         token.column = m_tokenColumn;
         token.position = m_tokenPos;
         token.length = m_pos - m_tokenPos;
         m_tokens << token;
      }

      void error(int line, int column, int length, const QString& message)
      {
         if (m_errors)
         {
            m_errors->append(DbmlError{line, column, length, message});
         }
      }

      void skipBlockComment()
      {
         const int line = m_line;
         const int column = m_column;
         advance(2);
         while (m_pos < m_source.size() && !(m_source[m_pos] == '*' && peek(1) == '/'))
         {
            advance();
         }
         if (m_pos >= m_source.size())
         {
            error(line, column, 2, QStringLiteral("Unterminated comment"));
            return;
         }
         advance(2);
      }

      void lexString()
      {
         beginToken();
         if (m_source.midRef(m_pos, 3) == QLatin1String("'''"))
         {
            advance(3);
            QString value;
            while (m_pos < m_source.size() && m_source.midRef(m_pos, 3) != QLatin1String("'''"))
            {
               if (m_source[m_pos] == '\\' && peek(1) == '\'')
               {
                  value += '\'';
                  advance(2);
               }
               else
               {
                  value += m_source[m_pos];
                  advance();
               }
            }
            if (m_pos >= m_source.size())
            {
               error(m_tokenLine, m_tokenColumn, 3, QStringLiteral("Unterminated multi-line string"));
            }
            else
            {
               advance(3);
            }
            addToken(DbmlToken::String, DbmlLexer::dedent(value));
            return;
         }

         advance();
         QString value;
         while (m_pos < m_source.size() && m_source[m_pos] != '\'' && m_source[m_pos] != '\n')
         {
            if (m_source[m_pos] == '\\' && m_pos + 1 < m_source.size() && m_source[m_pos + 1] != '\n')
            {
               const QChar escaped = m_source[m_pos + 1];
               value += escaped == 'n' ? QChar('\n') : escaped == 't' ? QChar('\t') : escaped;
               advance(2);
            }
            else
            {
               value += m_source[m_pos];
               advance();
            }
         }
         if (m_pos >= m_source.size() || m_source[m_pos] == '\n')
         {
            error(m_tokenLine, m_tokenColumn, m_pos - m_tokenPos, QStringLiteral("Unterminated string"));
         }
         else
         {
            advance();
         }
         addToken(DbmlToken::String, value);
      }

      void lexQuoted(QChar quote, DbmlToken::Kind kind, const QString& unterminatedMessage)
      {
         beginToken();
         advance();
         QString value;
         while (m_pos < m_source.size() && m_source[m_pos] != quote && m_source[m_pos] != '\n')
         {
            if (m_source[m_pos] == '\\' && peek(1) == quote)
            {
               value += quote;
               advance(2);
            }
            else
            {
               value += m_source[m_pos];
               advance();
            }
         }
         if (m_pos >= m_source.size() || m_source[m_pos] == '\n')
         {
            error(m_tokenLine, m_tokenColumn, m_pos - m_tokenPos, unterminatedMessage);
         }
         else
         {
            advance();
         }
         addToken(kind, value);
      }

      void lexColor()
      {
         beginToken();
         advance();
         while (m_pos < m_source.size() && isIdentifierPart(m_source[m_pos]))
         {
            advance();
         }
         const QString text = m_source.mid(m_tokenPos, m_pos - m_tokenPos);
         if (text.size() != 4 && text.size() != 7)
         {
            error(m_tokenLine, m_tokenColumn, text.size(), QStringLiteral("Invalid color '%1'").arg(text));
         }
         addToken(DbmlToken::Color, text);
      }

      void lexNumberOrIdentifier()
      {
         beginToken();
         while (m_pos < m_source.size() && m_source[m_pos].isDigit())
         {
            advance();
         }
         if (m_pos < m_source.size() && m_source[m_pos] == '.' && peek(1).isDigit())
         {
            advance();
            while (m_pos < m_source.size() && m_source[m_pos].isDigit())
            {
               advance();
            }
         }
         // 2fa_enabled is a name, not a number
         if (m_pos < m_source.size() && isIdentifierPart(m_source[m_pos]))
         {
            while (m_pos < m_source.size() && isIdentifierPart(m_source[m_pos]))
            {
               advance();
            }
            addToken(DbmlToken::Identifier, m_source.mid(m_tokenPos, m_pos - m_tokenPos));
            return;
         }
         addToken(DbmlToken::Number, m_source.mid(m_tokenPos, m_pos - m_tokenPos));
      }

      void lexIdentifier()
      {
         beginToken();
         while (m_pos < m_source.size() && isIdentifierPart(m_source[m_pos]))
         {
            advance();
         }
         addToken(DbmlToken::Identifier, m_source.mid(m_tokenPos, m_pos - m_tokenPos));
      }

      const QString& m_source;
      QVector<DbmlError>* m_errors = nullptr;
      QVector<DbmlToken> m_tokens;
      int m_pos = 0;
      int m_line = 1;
      int m_column = 1;
      int m_tokenPos = 0;
      int m_tokenLine = 1;
      int m_tokenColumn = 1;
   };
}

QVector<DbmlToken> DbmlLexer::tokenize(const QString &source, QVector<DbmlError> *errors)
{
   return Lexer(source, errors).run();
}

QString DbmlLexer::dedent(const QString &text)
{
   QStringList lines = text.split('\n');
   for (QString& line : lines)
   {
      if (line.endsWith('\r'))
      {
         line.chop(1);
      }
   }
   if (!lines.isEmpty() && lines.first().trimmed().isEmpty())
   {
      lines.removeFirst();
   }
   if (!lines.isEmpty() && lines.last().trimmed().isEmpty())
   {
      lines.removeLast();
   }

   int indent = INT_MAX;
   for (const QString& line : lines)
   {
      if (line.trimmed().isEmpty())
      {
         continue;
      }
      int spaces = 0;
      while (spaces < line.size() && (line[spaces] == ' ' || line[spaces] == '\t'))
      {
         ++spaces;
      }
      indent = std::min(indent, spaces);
   }
   if (indent == INT_MAX)
   {
      indent = 0;
   }

   for (QString& line : lines)
   {
      line = line.trimmed().isEmpty() ? QString() : line.mid(indent);
   }
   return lines.join('\n');
}
