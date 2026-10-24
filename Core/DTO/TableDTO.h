#pragma once
#include <QString>
#include <QVector>
#include "ColumnDTO.h"
#include "IndexDTO.h"

// A table without an explicit schema belongs to the default one
inline QString qualifiedName(const QString& schema, const QString& name)
{
   return (schema.isEmpty() ? QStringLiteral("public") : schema) + '.' + name;
}

struct TableDTO
{
   QString schema; // empty if not written
   QString name;
   QString alias;
   QString note;
   QString headerColor; // #rrggbb or empty
   QVector<ColumnDTO> columns;
   QVector<IndexDTO> indexes;
   int line = 0;    // first line of the definition
   int endLine = 0; // line of the closing brace

   QString fullName() const
   {
      return qualifiedName(schema, name);
   }

   // How the table is titled on the diagram
   QString displayName() const
   {
      return (schema.isEmpty() || schema == QLatin1String("public")) ? name : schema + '.' + name;
   }

   int columnIndex(const QString& columnName) const
   {
      for (int i = 0; i < columns.size(); ++i)
      {
         if (columns[i].name == columnName)
         {
            return i;
         }
      }
      return -1;
   }
};
