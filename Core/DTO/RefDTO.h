#pragma once
#include <QString>
#include <QStringList>
#include "TableDTO.h"

enum class Cardinality
{
   One,
   Many
};

struct RefEndpointDTO
{
   QString schema; // after parsing: the schema of the referenced table as written in its definition
   QString table;  // after parsing: the real table name (an alias is resolved)
   QStringList columns;

   QString tableFullName() const
   {
      return qualifiedName(schema, table);
   }
};

struct RefDTO
{
   QString name;
   RefEndpointDTO left;
   RefEndpointDTO right;
   // left > right: many-to-one, left < right: one-to-many, left - right: one-to-one, left <> right: many-to-many
   Cardinality leftCardinality = Cardinality::Many;
   Cardinality rightCardinality = Cardinality::One;
   QString onDelete; // cascade, restrict, set null, set default, no action
   QString onUpdate;
   QString color;
   bool isInline = false; // written as a column setting [ref: > table.column]
   int line = 0;

   QString relationSymbol() const
   {
      if (leftCardinality == Cardinality::Many && rightCardinality == Cardinality::One)
      {
         return QStringLiteral(">");
      }
      if (leftCardinality == Cardinality::One && rightCardinality == Cardinality::Many)
      {
         return QStringLiteral("<");
      }
      if (leftCardinality == Cardinality::One)
      {
         return QStringLiteral("-");
      }
      return QStringLiteral("<>");
   }
};
