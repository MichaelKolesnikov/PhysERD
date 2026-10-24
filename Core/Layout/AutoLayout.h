#pragma once
#include "Core_global.h"
#include <QMap>
#include <QPointF>
#include <QSizeF>
#include <QString>
struct SchemaDTO;

namespace AutoLayout
{
   // Places related tables in columns: a referenced table (the "one" side) is to the left of the tables
   // referencing it, the order inside a column reduces crossings. Unrelated tables go to a grid below.
   // The tables of a group are kept together, clear of the others.
   // sizes: table full name -> size on the diagram. Returns table full name -> top left corner
   CORE_EXPORT QMap<QString, QPointF> arrange(const SchemaDTO&, const QMap<QString, QSizeF>& sizes);
}
