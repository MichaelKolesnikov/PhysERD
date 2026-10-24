#pragma once
#include <QString>
#include <QVector>

struct IndexColumnDTO
{
   QString value;             // column name or expression text
   bool isExpression = false; // `expression` in DBML
};

struct IndexDTO
{
   QVector<IndexColumnDTO> columns;
   bool pk = false;
   bool unique = false;
   QString name;
   QString type; // btree, hash
   QString note;
   int line = 0;
};
