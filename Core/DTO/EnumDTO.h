#pragma once
#include <QString>
#include <QVector>
#include "TableDTO.h"

struct EnumValueDTO
{
   QString name;
   QString note;
};

struct EnumDTO
{
   QString schema;
   QString name;
   QVector<EnumValueDTO> values;
   int line = 0;

   QString fullName() const
   {
      return qualifiedName(schema, name);
   }
};
