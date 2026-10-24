#pragma once
#include <QString>

// A sticky note: Note name { '...' }
struct NoteDTO
{
   QString name;
   QString content;
   QString headerColor;
   int line = 0;
};
