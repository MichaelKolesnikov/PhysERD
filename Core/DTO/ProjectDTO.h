#pragma once
#include <QString>
#include <QMap>

struct ProjectDTO
{
   bool defined = false;
   QString name;
   QString databaseType;
   QString note;
   QMap<QString, QString> settings; // other key: 'value' pairs
   int line = 0;
};
