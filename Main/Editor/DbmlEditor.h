#pragma once
#include "Main_global.h"
#include <QPlainTextEdit>
#include "PIMPL.h"
#include "Dbml/DbmlError.h"
struct Theme;

// The DBML text: line numbers, syntax highlighting, parse errors under the text and in the gutter,
// auto-indentation and closing brackets
class MAIN_EXPORT DbmlEditor : public QPlainTextEdit
{
   Q_OBJECT
public:
   explicit DbmlEditor(QWidget* parent = nullptr);
   ~DbmlEditor() override;

   void setErrors(const QVector<DbmlError>&);
   void setTheme(const Theme&);
   void setEditorFont(const QFont&);
   void zoomText(int steps);

   // 1-based
   int currentLine() const;
   void goToLine(int line);

   void toggleComment();
   // Highlights all occurrences, returns their count
   int setSearchHighlight(const QString& text, bool caseSensitive);

   // used by the line number area
   int gutterWidth() const;
   void paintGutter(QPaintEvent*);
   void showErrorToolTip(const QPoint& viewportPos, const QPoint& globalPos);

signals:
   void fontSizeChanged(qreal pointSize);

protected:
   bool viewportEvent(QEvent*) override;
   void resizeEvent(QResizeEvent*) override;
   void keyPressEvent(QKeyEvent*) override;
   void wheelEvent(QWheelEvent*) override;

PIMPL
};
