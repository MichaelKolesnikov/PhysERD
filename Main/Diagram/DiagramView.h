#pragma once
#include "Main_global.h"
#include <QGraphicsView>
#include "PIMPL.h"

// Zoom with Ctrl+wheel or pinch, pan by dragging the empty canvas, with the middle button or with Space held.
// Shift+drag on the canvas selects with a rubber band
class MAIN_EXPORT DiagramView : public QGraphicsView
{
   Q_OBJECT
public:
   explicit DiagramView(QWidget* parent = nullptr);
   ~DiagramView() override;

   qreal zoom() const;
   void setZoom(qreal);
   void zoomIn();
   void zoomOut();
   void resetZoom();
   void fitToContents();

signals:
   void zoomChanged(qreal);

protected:
   bool viewportEvent(QEvent*) override;
   void wheelEvent(QWheelEvent*) override;
   void mousePressEvent(QMouseEvent*) override;
   void mouseMoveEvent(QMouseEvent*) override;
   void mouseReleaseEvent(QMouseEvent*) override;
   void keyPressEvent(QKeyEvent*) override;
   void keyReleaseEvent(QKeyEvent*) override;

PIMPL
};
