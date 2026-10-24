#pragma once
#include <QSyntaxHighlighter>
#include <QTextCharFormat>
#include "Theme.h"

class DbmlHighlighter : public QSyntaxHighlighter
{
   Q_OBJECT
public:
   explicit DbmlHighlighter(QTextDocument* parent);

   void setTheme(const Theme&);

protected:
   void highlightBlock(const QString& text) override;

private:
   enum State
   {
      Normal = 0,
      InMultiLineString = 1,
      InBlockComment = 2
   };

   QTextCharFormat m_keyword;
   QTextCharFormat m_setting;
   QTextCharFormat m_type;
   QTextCharFormat m_string;
   QTextCharFormat m_expression;
   QTextCharFormat m_comment;
   QTextCharFormat m_number;
   QTextCharFormat m_symbol;
   QTextCharFormat m_quotedName;
};
