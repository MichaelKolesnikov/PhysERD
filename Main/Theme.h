#pragma once
#include "Main_global.h"
#include <QColor>
#include <QPalette>

// Colors of the diagram, the editor and the window, for the light and the dark look
struct MAIN_EXPORT Theme
{
   bool dark = false;

   // diagram
   QColor canvas;
   QColor gridDot;
   QColor tableBody;
   QColor tableBorder;
   QColor tableShadow;
   QColor defaultHeader;
   QColor columnText;
   QColor typeText;
   QColor rowSeparator;
   QColor rowHover;
   QColor rowHighlight;
   QColor keyIcon;
   QColor noteMark;
   QColor refLine;
   QColor refHighlight;
   QColor selection;
   QColor groupDefault;
   QColor noteBody;
   QColor noteHeader;
   QColor noteText;

   // editor
   QColor editorBackground;
   QColor editorText;
   QColor gutterBackground;
   QColor gutterText;
   QColor currentLine;
   QColor keyword;
   QColor setting;
   QColor type;
   QColor string;
   QColor expression;
   QColor comment;
   QColor number;
   QColor symbol;
   QColor error;
   QColor findMatch;

   static Theme light();
   static Theme darkTheme();

   QPalette palette() const;
   // Readable text over a color: black on light ones, white on dark ones
   static QColor textOn(const QColor& background);
};
