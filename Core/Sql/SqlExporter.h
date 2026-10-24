#pragma once
#include "Core_global.h"
#include <QString>
struct SchemaDTO;

namespace SqlExporter
{
   enum class Dialect
   {
      PostgreSQL,
      MySQL,
      SQLServer
   };

   // CREATE statements for the enums, tables, indexes, comments and foreign keys.
   // A many-to-many ref becomes a junction table
   CORE_EXPORT QString exportSchema(const SchemaDTO&, Dialect);

   // The dialect named by Project { database_type: '...' }, fallback if it names none of them
   CORE_EXPORT Dialect dialectFromDatabaseType(const QString& databaseType, Dialect fallback);
   CORE_EXPORT QString dialectName(Dialect);
}
