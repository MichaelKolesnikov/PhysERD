#include "SchemaDTO.h"

int SchemaDTO::tableIndex(const QString &fullName) const
{
   for (int i = 0; i < tables.size(); ++i)
   {
      if (tables[i].fullName() == fullName)
      {
         return i;
      }
   }
   return -1;
}

const TableDTO* SchemaDTO::table(const QString &fullName) const
{
   const int index = tableIndex(fullName);
   return index < 0 ? nullptr : &tables[index];
}

const EnumDTO* SchemaDTO::enumForType(const QString &type) const
{
   const QString qualifiedType = type.contains('.') ? type : qualifiedName(QString(), type);
   for (const EnumDTO& enumDTO : enums)
   {
      if (enumDTO.fullName() == qualifiedType)
      {
         return &enumDTO;
      }
   }
   return nullptr;
}
