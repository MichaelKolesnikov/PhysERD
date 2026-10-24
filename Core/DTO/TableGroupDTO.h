#pragma once
#include <QString>
#include <QStringList>

struct TableGroupDTO
{
   QString name;
   QStringList tables; // full names (schema.table) after parsing
   QString color;
   QString note;
   int line = 0;
};
