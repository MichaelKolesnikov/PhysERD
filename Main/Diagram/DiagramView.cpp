#include "DiagramView.h"
#include "DiagramScene.h"
#include <QWheelEvent>
#include <QScrollBar>
#include <QNativeGestureEvent>
#include <algorithm>
#include <cmath>

namespace
{
   const qreal MinZoom = 0.1;
   const qreal MaxZoom = 4.0;
   const qreal ZoomStep = 1.15;
}

struct DiagramView::Impl
{
   explicit Impl(DiagramView* view) : view(view) {}

   void scaleBy(qreal factor);

   DiagramView* view = nullptr;
   bool panning = false;
   bool panMoved = false;
   bool spaceHeld = false;
   bool rubberBand = false;
   QPoint panStart;
};

DiagramView::DiagramView(QWidget *parent)
   : QGraphicsView(parent)
{
   pimpl = std::make_unique<Impl>(this);
   setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing | QPainter::SmoothPixmapTransform);
   setViewportUpdateMode(QGraphicsView::FullViewportUpdate);
   setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
   setResizeAnchor(QGraphicsView::AnchorViewCenter);
   setDragMode(QGraphicsView::NoDrag);
   setFrameShape(QFrame::NoFrame);
   setFocusPolicy(Qt::StrongFocus);
   viewport()->grabGesture(Qt::PinchGesture);
}

DiagramView::~DiagramView() = default;

qreal DiagramView::zoom() const
{
   return transform().m11();
}

void DiagramView::Impl::scaleBy(qreal factor)
{
   const qreal current = view->zoom();
   const qreal target = std::clamp(current * factor, MinZoom, MaxZoom);
   if (std::abs(target - current) < 1e-6)
   {
      return;
   }
   view->scale(target / current, target / current);
   emit view->zoomChanged(view->zoom());
}

void DiagramView::setZoom(qreal zoom)
{
   const ViewportAnchor anchor = transformationAnchor();
   setTransformationAnchor(QGraphicsView::AnchorViewCenter);
   pimpl->scaleBy(zoom / this->zoom());
   setTransformationAnchor(anchor);
}

void DiagramView::zoomIn()
{
   setZoom(zoom() * ZoomStep);
}

void DiagramView::zoomOut()
{
   setZoom(zoom() / ZoomStep);
}

void DiagramView::resetZoom()
{
   setZoom(1.0);
}

void DiagramView::fitToContents()
{
   auto diagram = qobject_cast<DiagramScene*>(scene());
   if (!diagram)
   {
      return;
   }
   const QRectF content = diagram->contentRect();
   if (content.isNull())
   {
      resetZoom();
      centerOn(0, 0);
      return;
   }
   const QRectF target = content.adjusted(-40, -40, 40, 40);
   const QSizeF available = viewport()->size();
   const qreal zoom = std::clamp(std::min(available.width() / target.width(), available.height() / target.height()), MinZoom, 1.25);
   setTransform(QTransform::fromScale(zoom, zoom));
   centerOn(target.center());
   emit zoomChanged(this->zoom());
}

bool DiagramView::viewportEvent(QEvent *event)
{
   if (event->type() == QEvent::NativeGesture)
   {
      auto gesture = static_cast<QNativeGestureEvent*>(event);
      if (gesture->gestureType() == Qt::ZoomNativeGesture)
      {
         pimpl->scaleBy(1.0 + gesture->value());
         return true;
      }
   }
   return QGraphicsView::viewportEvent(event);
}

void DiagramView::wheelEvent(QWheelEvent *event)
{
   if (event->modifiers() & Qt::ControlModifier)
   {
      // 15% per notch (120); touchpads send fractions of a notch
      pimpl->scaleBy(std::pow(ZoomStep, event->angleDelta().y() / 120.0));
      event->accept();
      return;
   }
   QGraphicsView::wheelEvent(event);
}

void DiagramView::mousePressEvent(QMouseEvent *event)
{
   const bool onItem = itemAt(event->pos()) != nullptr;
   const bool panButton = event->button() == Qt::MiddleButton
         || (event->button() == Qt::LeftButton && (pimpl->spaceHeld || (!onItem && !(event->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier)))));
   if (panButton)
   {
      pimpl->panning = true;
      pimpl->panMoved = false;
      pimpl->panStart = event->pos();
      viewport()->setCursor(Qt::ClosedHandCursor);
      event->accept();
      return;
   }
   if (event->button() == Qt::LeftButton && !onItem && (event->modifiers() & Qt::ShiftModifier))
   {
      pimpl->rubberBand = true;
      setDragMode(QGraphicsView::RubberBandDrag);
   }
   QGraphicsView::mousePressEvent(event);
}

void DiagramView::mouseMoveEvent(QMouseEvent *event)
{
   if (pimpl->panning)
   {
      const QPoint delta = event->pos() - pimpl->panStart;
      if (delta.manhattanLength() > 0)
      {
         pimpl->panMoved = true;
      }
      horizontalScrollBar()->setValue(horizontalScrollBar()->value() - delta.x());
      verticalScrollBar()->setValue(verticalScrollBar()->value() - delta.y());
      pimpl->panStart = event->pos();
      event->accept();
      return;
   }
   QGraphicsView::mouseMoveEvent(event);
}

void DiagramView::mouseReleaseEvent(QMouseEvent *event)
{
   if (pimpl->panning && (event->button() == Qt::LeftButton || event->button() == Qt::MiddleButton))
   {
      pimpl->panning = false;
      viewport()->setCursor(pimpl->spaceHeld ? Qt::OpenHandCursor : Qt::ArrowCursor);
      // a click on the empty canvas clears the selection
      if (!pimpl->panMoved && scene() && event->button() == Qt::LeftButton)
      {
         scene()->clearSelection();
      }
      event->accept();
      return;
   }
   QGraphicsView::mouseReleaseEvent(event);
   if (pimpl->rubberBand)
   {
      pimpl->rubberBand = false;
      setDragMode(QGraphicsView::NoDrag);
   }
}

void DiagramView::keyPressEvent(QKeyEvent *event)
{
   if (event->key() == Qt::Key_Space && !event->isAutoRepeat())
   {
      pimpl->spaceHeld = true;
      viewport()->setCursor(Qt::OpenHandCursor);
      event->accept();
      return;
   }
   QGraphicsView::keyPressEvent(event);
}

void DiagramView::keyReleaseEvent(QKeyEvent *event)
{
   if (event->key() == Qt::Key_Space && !event->isAutoRepeat())
   {
      pimpl->spaceHeld = false;
      viewport()->setCursor(Qt::ArrowCursor);
      event->accept();
      return;
   }
   QGraphicsView::keyReleaseEvent(event);
}
