#include "StickyNoteItem.h"
#include "DiagramScene.h"
#include "DiagramStyle.h"
#include <QPainter>
#include <QGraphicsSceneMouseEvent>
#include <QFontMetricsF>
#include <cmath>

namespace
{
   const qreal Width = 240;
   const qreal Padding = 12;
   const qreal HeaderHeight = 28;
}

StickyNoteItem::StickyNoteItem(const NoteDTO &note, const DiagramStyle *style)
   : m_note(note), m_style(style)
{
   setFlags(ItemIsSelectable | ItemIsMovable | ItemSendsGeometryChanges);
   setZValue(12);
   relayout();
}

int StickyNoteItem::type() const
{
   return Type;
}

const NoteDTO& StickyNoteItem::note() const
{
   return m_note;
}

void StickyNoteItem::relayout()
{
   prepareGeometryChange();
   const QFontMetricsF metrics(m_style->columnFont);
   const QRectF text = metrics.boundingRect(QRectF(0, 0, Width - Padding * 2, 10000), Qt::TextWordWrap, m_note.content);
   m_rect = QRectF(0, 0, Width, std::ceil(HeaderHeight + text.height() + Padding * 2));
}

QRectF StickyNoteItem::boundingRect() const
{
   return m_rect.adjusted(-2, -2, 4, 4);
}

void StickyNoteItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *, QWidget *)
{
   const Theme& theme = m_style->theme;
   painter->setRenderHint(QPainter::Antialiasing);
   painter->setPen(Qt::NoPen);
   painter->setBrush(theme.tableShadow);
   painter->drawRoundedRect(m_rect.translated(1.5, 3), 4, 4);
   painter->setBrush(theme.noteBody);
   painter->drawRoundedRect(m_rect, 4, 4);

   const QColor header = QColor(m_note.headerColor).isValid() ? QColor(m_note.headerColor) : theme.noteHeader;
   QPainterPath top;
   top.addRoundedRect(QRectF(0, 0, m_rect.width(), HeaderHeight), 4, 4);
   QPainterPath bottom;
   bottom.addRect(QRectF(0, HeaderHeight / 2, m_rect.width(), HeaderHeight / 2));
   painter->setBrush(header);
   painter->drawPath(top.united(bottom));

   QFont title = m_style->headerFont;
   painter->setFont(title);
   painter->setPen(Theme::textOn(header));
   painter->drawText(QRectF(Padding, 0, m_rect.width() - Padding * 2, HeaderHeight), Qt::AlignVCenter | Qt::AlignLeft, m_note.name);

   painter->setFont(m_style->columnFont);
   painter->setPen(theme.noteText);
   painter->drawText(QRectF(Padding, HeaderHeight + Padding, m_rect.width() - Padding * 2, m_rect.height() - HeaderHeight - Padding * 2),
                     Qt::TextWordWrap | Qt::AlignLeft | Qt::AlignTop, m_note.content);

   if (isSelected() && !m_style->exporting)
   {
      painter->setBrush(Qt::NoBrush);
      painter->setPen(QPen(theme.selection, 2));
      painter->drawRoundedRect(m_rect, 4, 4);
   }
}

QVariant StickyNoteItem::itemChange(GraphicsItemChange change, const QVariant &value)
{
   if (change == ItemPositionChange && m_style->snapToGrid)
   {
      const QPointF point = value.toPointF();
      const qreal grid = m_style->gridSize;
      return QPointF(std::round(point.x() / grid) * grid, std::round(point.y() / grid) * grid);
   }
   if (change == ItemPositionHasChanged)
   {
      m_moved = true;
   }
   return QGraphicsItem::itemChange(change, value);
}

void StickyNoteItem::mousePressEvent(QGraphicsSceneMouseEvent *event)
{
   m_moved = false;
   QGraphicsItem::mousePressEvent(event);
}

void StickyNoteItem::mouseReleaseEvent(QGraphicsSceneMouseEvent *event)
{
   QGraphicsItem::mouseReleaseEvent(event);
   auto diagramScene = static_cast<DiagramScene*>(scene());
   if (m_moved)
   {
      diagramScene->moveFinished();
   }
   else if (event->button() == Qt::LeftButton)
   {
      diagramScene->activate(m_note.line);
   }
}
