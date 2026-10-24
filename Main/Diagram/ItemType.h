#pragma once
#include <QGraphicsItem>

namespace ItemType
{
   enum
   {
      Table = QGraphicsItem::UserType + 1,
      Ref,
      TableGroup,
      StickyNote
   };
}
