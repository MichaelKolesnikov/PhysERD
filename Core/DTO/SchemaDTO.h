#pragma once
#include "Core_global.h"
#include <QVector>
#include "ProjectDTO.h"
#include "TableDTO.h"
#include "RefDTO.h"
#include "EnumDTO.h"
#include "TableGroupDTO.h"
#include "NoteDTO.h"

struct CORE_EXPORT SchemaDTO
{
   ProjectDTO project;
   QVector<TableDTO> tables;
   QVector<RefDTO> refs;
   QVector<EnumDTO> enums;
   QVector<TableGroupDTO> tableGroups;
   QVector<NoteDTO> notes;

   int tableIndex(const QString& fullName) const;
   const TableDTO* table(const QString& fullName) const;
   // The enum a column type names: status, schema.status
   const EnumDTO* enumForType(const QString& type) const;
};
