#pragma once
#include "Core_global.h"
#include <QVector>
#include "DTO/SchemaDTO.h"
#include "DbmlError.h"

struct DbmlParseResult
{
   SchemaDTO schema;
   QVector<DbmlError> errors; // sorted by position

   bool ok() const
   {
      return errors.isEmpty();
   }
};

namespace DbmlParser
{
   // Parses as much as possible: a broken element is reported and skipped, the rest is still parsed.
   // Refs and table groups are resolved after parsing: aliases are replaced by the real table names
   CORE_EXPORT DbmlParseResult parse(const QString& source);
}
