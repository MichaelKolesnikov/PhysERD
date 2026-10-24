#pragma once
#include <QString>

struct ColumnDTO
{
   enum DefaultKind
   {
      NoDefault,
      StringDefault,     // 'text'
      NumberDefault,     // 1, -2.5
      ExpressionDefault, // `now()`
      BooleanDefault,    // true, false
      NullDefault        // null
   };

   QString name;
   // As written in DBML without quotes: integer, varchar(255), timestamp with time zone, schema.enum_name
   QString type;
   bool pk = false;
   bool increment = false;
   bool notNull = false;
   bool unique = false;
   DefaultKind defaultKind = NoDefault;
   QString defaultValue;
   QString note;
   int line = 0;
};
