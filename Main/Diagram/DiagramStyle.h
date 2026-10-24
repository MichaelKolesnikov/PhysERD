#pragma once
#include <QFont>
#include "Theme.h"

// What all the diagram items are drawn with; owned by the scene, the items keep a pointer to it
struct DiagramStyle
{
   enum class Detail
   {
      AllFields,
      KeysOnly,   // primary keys and the columns taking part in refs
      TableNames
   };

   enum class Notation
   {
      Labels,    // 1 and * near the ends
      CrowsFoot
   };

   Theme theme = Theme::light();
   Detail detail = Detail::AllFields;
   Notation notation = Notation::Labels;
   bool snapToGrid = false;
   bool exporting = false; // no grid, no hover and selection marks

   QFont headerFont;
   QFont columnFont;
   QFont typeFont;
   qreal headerHeight = 34;
   qreal rowHeight = 28;
   qreal minTableWidth = 200;
   qreal gridSize = 20;
};
