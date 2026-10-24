#pragma once
#include <QMap>
#include <QPointF>
#include <QString>

// Where the diagram elements are: the DBML text does not keep positions, they are saved next to it
struct LayoutDTO
{
   QMap<QString, QPointF> tables; // table full name (schema.table) -> top left corner
   QMap<QString, QPointF> notes;  // sticky note name -> top left corner
   QMap<QString, QString> tableColors; // table full name -> header color chosen on the diagram

   bool isEmpty() const
   {
      return tables.isEmpty() && notes.isEmpty();
   }
};
