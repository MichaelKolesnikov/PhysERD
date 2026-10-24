#pragma once
#include <QGraphicsItem>
#include "DTO/NoteDTO.h"
#include "ItemType.h"
struct DiagramStyle;

// Note name { '...' } drawn as a yellow sticky note
class StickyNoteItem : public QGraphicsItem
{
public:
   enum { Type = ItemType::StickyNote };

   StickyNoteItem(const NoteDTO&, const DiagramStyle*);

   int type() const override;
   const NoteDTO& note() const;
   void relayout();

   QRectF boundingRect() const override;
   void paint(QPainter*, const QStyleOptionGraphicsItem*, QWidget*) override;

protected:
   QVariant itemChange(GraphicsItemChange, const QVariant&) override;
   void mousePressEvent(QGraphicsSceneMouseEvent*) override;
   void mouseReleaseEvent(QGraphicsSceneMouseEvent*) override;

private:
   NoteDTO m_note;
   const DiagramStyle* m_style = nullptr;
   QRectF m_rect;
   bool m_moved = false;
};
