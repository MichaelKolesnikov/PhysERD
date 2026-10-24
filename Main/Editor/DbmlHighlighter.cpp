#include "DbmlHighlighter.h"
#include <QRegularExpression>

DbmlHighlighter::DbmlHighlighter(QTextDocument *parent)
   : QSyntaxHighlighter(parent)
{
   setTheme(Theme::light());
}

void DbmlHighlighter::setTheme(const Theme &theme)
{
   m_keyword.setForeground(theme.keyword);
   m_keyword.setFontWeight(QFont::Bold);
   m_setting.setForeground(theme.setting);
   m_type.setForeground(theme.type);
   m_string.setForeground(theme.string);
   m_expression.setForeground(theme.expression);
   m_comment.setForeground(theme.comment);
   m_comment.setFontItalic(true);
   m_number.setForeground(theme.number);
   m_symbol.setForeground(theme.symbol);
   m_symbol.setFontWeight(QFont::Bold);
   m_quotedName.setForeground(theme.editorText);
   m_quotedName.setFontWeight(QFont::DemiBold);
   rehighlight();
}

void DbmlHighlighter::highlightBlock(const QString &text)
{
   // strings, expressions and comments first: nothing else is highlighted inside them
   QVector<bool> taken(text.size(), false);
   auto mark = [&](int start, int length, const QTextCharFormat& format) {
      setFormat(start, length, format);
      for (int i = start; i < start + length && i < taken.size(); ++i)
      {
         taken[i] = true;
      }
   };

   int pos = 0;
   setCurrentBlockState(Normal);
   if (previousBlockState() == InMultiLineString)
   {
      const int end = text.indexOf(QLatin1String("'''"));
      if (end < 0)
      {
         mark(0, text.size(), m_string);
         setCurrentBlockState(InMultiLineString);
         return;
      }
      mark(0, end + 3, m_string);
      pos = end + 3;
   }
   else if (previousBlockState() == InBlockComment)
   {
      const int end = text.indexOf(QLatin1String("*/"));
      if (end < 0)
      {
         mark(0, text.size(), m_comment);
         setCurrentBlockState(InBlockComment);
         return;
      }
      mark(0, end + 2, m_comment);
      pos = end + 2;
   }

   while (pos < text.size())
   {
      const QChar c = text[pos];
      if (c == '/' && pos + 1 < text.size() && text[pos + 1] == '/')
      {
         mark(pos, text.size() - pos, m_comment);
         break;
      }
      if (c == '/' && pos + 1 < text.size() && text[pos + 1] == '*')
      {
         const int end = text.indexOf(QLatin1String("*/"), pos + 2);
         if (end < 0)
         {
            mark(pos, text.size() - pos, m_comment);
            setCurrentBlockState(InBlockComment);
            break;
         }
         mark(pos, end + 2 - pos, m_comment);
         pos = end + 2;
         continue;
      }
      if (text.midRef(pos, 3) == QLatin1String("'''"))
      {
         const int end = text.indexOf(QLatin1String("'''"), pos + 3);
         if (end < 0)
         {
            mark(pos, text.size() - pos, m_string);
            setCurrentBlockState(InMultiLineString);
            break;
         }
         mark(pos, end + 3 - pos, m_string);
         pos = end + 3;
         continue;
      }
      if (c == '\'' || c == '"' || c == '`')
      {
         int end = pos + 1;
         while (end < text.size() && text[end] != c)
         {
            end += text[end] == '\\' ? 2 : 1;
         }
         const int length = std::min(end + 1, text.size()) - pos;
         mark(pos, length, c == '\'' ? m_string : c == '`' ? m_expression : m_quotedName);
         pos += length;
         continue;
      }
      ++pos;
   }

   auto apply = [&](const QRegularExpression& expression, const QTextCharFormat& format, int group = 0) {
      auto it = expression.globalMatch(text);
      while (it.hasNext())
      {
         const QRegularExpressionMatch match = it.next();
         const int start = match.capturedStart(group);
         if (start >= 0 && start < taken.size() && !taken[start])
         {
            setFormat(start, match.capturedLength(group), format);
         }
      }
   };

   static const QRegularExpression keywords(
         QStringLiteral("\\b(Table|TablePartial|Ref|Enum|TableGroup|Project|Note|indexes|as|Records)\\b"),
         QRegularExpression::CaseInsensitiveOption);
   static const QRegularExpression types(
         QStringLiteral("\\b(int|int2|int4|int8|integer|bigint|smallint|tinyint|mediumint|serial|bigserial|smallserial|"
                        "decimal|numeric|real|float|float4|float8|double|money|bool|boolean|bit|"
                        "char|varchar|nvarchar|nchar|character|text|tinytext|mediumtext|longtext|citext|string|"
                        "date|time|timetz|timestamp|timestamptz|datetime|datetime2|interval|year|"
                        "uuid|json|jsonb|xml|bytea|blob|binary|varbinary|inet|cidr|macaddr|point|geometry|enum)\\b"),
         QRegularExpression::CaseInsensitiveOption);
   // the second word of an indented line is a column type: "  name varchar(255)"
   static const QRegularExpression columnType(QStringLiteral("^\\s+[A-Za-z_][\\w]*\\s+([A-Za-z_][\\w.]*)"));
   static const QRegularExpression numbers(QStringLiteral("(?<![\\w#])-?\\d+(\\.\\d+)?\\b"));
   static const QRegularExpression colors(QStringLiteral("#[0-9A-Fa-f]{3,6}\\b"));
   static const QRegularExpression relations(QStringLiteral("(?<=\\s)(<>|<|>|-)(?=\\s)|(?<=:\\s)(<>|<|>|-)"));
   static const QRegularExpression settingsBlock(QStringLiteral("\\[[^\\]]*\\]"));
   static const QRegularExpression settings(
         QStringLiteral("\\b(pk|primary key|not null|null|unique|increment|default|note|ref|headercolor|color|delete|update|"
                        "cascade|restrict|set null|set default|no action|name|type|btree|hash|check|true|false)\\b"),
         QRegularExpression::CaseInsensitiveOption);

   apply(types, m_type);
   const QRegularExpressionMatch column = columnType.match(text);
   if (column.hasMatch())
   {
      static const QRegularExpression notColumn(QStringLiteral("^\\s*(Note|Ref|indexes)\\b"), QRegularExpression::CaseInsensitiveOption);
      const int start = column.capturedStart(1);
      if (!notColumn.match(text).hasMatch() && !taken.value(start))
      {
         setFormat(start, column.capturedLength(1), m_type);
      }
   }
   apply(keywords, m_keyword);
   apply(numbers, m_number);
   apply(colors, m_number);
   apply(relations, m_symbol);

   auto blocks = settingsBlock.globalMatch(text);
   while (blocks.hasNext())
   {
      const QRegularExpressionMatch block = blocks.next();
      auto it = settings.globalMatch(block.captured());
      while (it.hasNext())
      {
         const QRegularExpressionMatch match = it.next();
         const int start = block.capturedStart() + match.capturedStart();
         if (!taken.value(start))
         {
            setFormat(start, match.capturedLength(), m_setting);
         }
      }
   }
}
