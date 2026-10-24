#pragma once
#include <QGraphicsItem>
#include "DTO/RefDTO.h"
#include "ItemType.h"
struct DiagramStyle;
class TableItem;

// A relationship line between the column rows of two tables
class RefItem : public QGraphicsItem
{
public:
   enum { Type = ItemType::Ref };

   RefItem(const RefDTO&, TableItem* left, TableItem* right, const DiagramStyle*);

   int type() const override;
   const RefDTO& ref() const;
   TableItem* leftTable() const;
   TableItem* rightTable() const;

   // Follows the tables: call after one of them moved or changed its size
   void updatePath();
   void setHighlighted(bool);

   QRectF boundingRect() const override;
   QPainterPath shape() const override;
   void paint(QPainter*, const QStyleOptionGraphicsItem*, QWidget*) override;

protected:
   void hoverEnterEvent(QGraphicsSceneHoverEvent*) override;
   void hoverLeaveEvent(QGraphicsSceneHoverEvent*) override;
   void mousePressEvent(QGraphicsSceneMouseEvent*) override;

private:
   void drawEnd(QPainter*, const QPointF& point, const QPointF& direction, Cardinality, const QColor&) const;
   QString tooltip() const;

   RefDTO m_ref;
   TableItem* m_left = nullptr;
   TableItem* m_right = nullptr;
   const DiagramStyle* m_style = nullptr;
   QPainterPath m_path;
   QPointF m_leftPoint;
   QPointF m_rightPoint;
   QPointF m_leftDirection;  // out of the left table
   QPointF m_rightDirection; // out of the right table
   bool m_highlighted = false;
};
