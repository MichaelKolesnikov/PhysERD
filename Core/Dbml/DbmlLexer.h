#pragma once
#include "Core_global.h"
#include <QString>
#include <QVector>
#include "DbmlError.h"

struct DbmlToken
{
   enum Kind
   {
      Identifier,
      QuotedIdentifier, // "name"; text holds the name without quotes
      String,           // 'text' or '''text'''; text holds the value
      Expression,       // `expression`; text holds the expression
      Number,
      Color,            // #rgb or #rrggbb including '#'
      Symbol,           // { } [ ] ( ) , : . < > - <> ~
      Newline,
      End
   };

   Kind kind = End;
   QString text;
   int line = 1;     // 1-based
   int column = 1;   // 1-based
   int position = 0; // offset in the source
   int length = 0;   // characters in the source
};

namespace DbmlLexer
{
   // Comments are dropped, consecutive line breaks give one Newline token, the last token is End
   CORE_EXPORT QVector<DbmlToken> tokenize(const QString& source, QVector<DbmlError>* errors = nullptr);

   // Removes the leading and trailing empty lines and the common indentation of a ''' string
   CORE_EXPORT QString dedent(const QString& text);
}
