#pragma once
#include "Main_global.h"
#include <QMainWindow>
#include "PIMPL.h"

// The DBML editor on the left, the live diagram on the right.
// A file.dbml keeps the positions of its tables in file.layout.json next to it
class MAIN_EXPORT MainWindow : public QMainWindow
{
   Q_OBJECT
public:
   explicit MainWindow(QWidget* parent = nullptr);
   ~MainWindow() override;

   bool openFile(const QString& path);
   bool importSql(const QString& path);
   void loadExample();
   bool hasFilePath() const;

protected:
   void closeEvent(QCloseEvent*) override;
   void showEvent(QShowEvent*) override;
   void dragEnterEvent(QDragEnterEvent*) override;
   void dropEvent(QDropEvent*) override;

PIMPL
};
