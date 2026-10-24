#pragma once
#include "Main_global.h"
#include <QWidget>
#include "PIMPL.h"
class DbmlEditor;

// Find and replace in the editor: Enter finds the next match, Shift+Enter the previous one, Escape closes
class MAIN_EXPORT FindBar : public QWidget
{
   Q_OBJECT
public:
   explicit FindBar(DbmlEditor* editor, QWidget* parent = nullptr);
   ~FindBar() override;

   void showFind();
   void showReplace();
   void findNext();
   void findPrevious();

protected:
   void keyPressEvent(QKeyEvent*) override;

PIMPL
};
