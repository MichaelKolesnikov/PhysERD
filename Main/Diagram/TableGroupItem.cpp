#include "TableGroupItem.h"
#include "TableItem.h"
#include "DiagramScene.h"
#include "DiagramStyle.h"
#include <QPainter>
#include <QGraphicsSceneMouseEvent>
#include <QFontMetricsF>
#include <cmath>

namespace
{
   const qreal Padding = 20;
   const qreal TitleHeight = 30;
}

TableGroupItem::TableGroupItem(const TableGroupDTO &group, const QVector<TableItem*> &tables, const DiagramStyle *style)
   : m_group(group), m_tables(tables), m_style(style)
{
   setZValue(0);
   setAcceptedMouseButtons(Qt::LeftButton);
   if (!group.note.isEmpty())
   {
      setToolTip(QStringLiteral("<b>%1</b><hr>%2").arg(group.name.toHtmlEscaped(), group.note.toHtmlEscaped()));
   }
   updateGeometry();
}

int TableGroupItem::type() const
{
   return Type;
}

const TableGroupDTO& TableGroupItem::group() const
{
   return m_group;
}

const QVector<TableItem*>& TableGroupItem::tables() const
{
   return m_tables;
}

void TableGroupItem::updateGeometry()
{
   prepareGeometryChange();
   QRectF bounds;
   for (TableItem* table : qAsConst(m_tables))
   {
      bounds = bounds.isNull() ? table->sceneCardRect() : bounds.united(table->sceneCardRect());
   }
   const qreal titleWidth = QFontMetricsF(m_style->headerFont).horizontalAdvance(m_group.name) + Padding * 2;
   m_rect = bounds.adjusted(-Padding, -Padding - TitleHeight, Padding, Padding);
   m_rect.setWidth(std::max(m_rect.width(), titleWidth));
   update();
}

QRectF TableGroupItem::boundingRect() const
{
   return m_rect.adjusted(-2, -2, 2, 2);
}

QColor TableGroupItem::color() const
{
   const QColor color(m_group.color);
   return color.isValid() ? color : m_style->theme.groupDefault;
}

void TableGroupItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *, QWidget *)
{
   painter->setRenderHint(QPainter::Antialiasing);
   const QColor base = color();
   QColor fill = base;
   fill.setAlpha(m_style->theme.dark ? 40 : 28);
   QColor border = base;
   border.setAlpha(m_dragging ? 255 : 150);

   painter->setPen(QPen(border, 1.5));
   painter->setBrush(fill);
   painter->drawRoundedRect(m_rect, 10, 10);

   QColor titleFill = base;
   titleFill.setAlpha(m_style->theme.dark ? 110 : 70);
   QPainterPath title;
   title.addRoundedRect(QRectF(m_rect.left(), m_rect.top(), m_rect.width(), TitleHeight), 10, 10);
   QPainterPath bottom;
   bottom.addRect(QRectF(m_rect.left(), m_rect.top() + TitleHeight / 2, m_rect.width(), TitleHeight / 2));
   painter->setPen(Qt::NoPen);
   painter->setBrush(titleFill);
   painter->drawPath(title.united(bottom));

   QFont font = m_style->headerFont;
   painter->setFont(font);
   painter->setPen(m_style->theme.dark ? base.lighter(150) : base.darker(160));
   painter->drawText(QRectF(m_rect.left() + 12, m_rect.top(), m_rect.width() - 24, TitleHeight), Qt::AlignVCenter | Qt::AlignLeft, m_group.name);
}

void TableGroupItem::mousePressEvent(QGraphicsSceneMouseEvent *event)
{
   m_dragging = true;
   m_moved = false;
   m_lastDragPos = event->scenePos();
   update();
   event->accept();
}

void TableGroupItem::mouseMoveEvent(QGraphicsSceneMouseEvent *event)
{
   if (!m_dragging)
   {
      return;
   }
   QPointF delta = event->scenePos() - m_lastDragPos;
   if (m_style->snapToGrid)
   {
      const qreal grid = m_style->gridSize;
      delta = QPointF(std::round(delta.x() / grid) * grid, std::round(delta.y() / grid) * grid);
      if (delta.isNull())
      {
         return;
      }
   }
   m_lastDragPos += delta;
   for (TableItem* table : qAsConst(m_tables))
   {
      table->moveBy(delta.x(), delta.y());
   }
   m_moved = true;
}

void TableGroupItem::mouseReleaseEvent(QGraphicsSceneMouseEvent *event)
{
   m_dragging = false;
   update();
   auto diagramScene = static_cast<DiagramScene*>(scene());
   if (m_moved)
   {
      diagramScene->moveFinished();
   }
   else if (event->button() == Qt::LeftButton)
   {
      diagramScene->activate(m_group.line);
   }
}
