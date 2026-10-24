#pragma once
#include "Core_global.h"
#include <QStringList>
#include "DTO/SchemaDTO.h"

struct SqlImportResult
{
   SchemaDTO schema;
   QStringList warnings; // statements that were skipped and why
};

namespace SqlImporter
{
   // Reads CREATE TABLE / CREATE TYPE ... AS ENUM / CREATE INDEX / ALTER TABLE ... ADD / COMMENT ON
   // statements of PostgreSQL, MySQL and SQL Server dumps; anything else is skipped with a warning
   CORE_EXPORT SqlImportResult import(const QString& sql);
}
