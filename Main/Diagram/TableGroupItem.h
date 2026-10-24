#pragma once
#include <QGraphicsItem>
#include "DTO/TableGroupDTO.h"
#include "ItemType.h"
struct DiagramStyle;
class TableItem;

// A frame around the tables of a TableGroup; dragging it moves them all
class TableGroupItem : public QGraphicsItem
{
public:
   enum { Type = ItemType::TableGroup };

   TableGroupItem(const TableGroupDTO&, const QVector<TableItem*>& tables, const DiagramStyle*);

   int type() const override;
   const TableGroupDTO& group() const;
   const QVector<TableItem*>& tables() const;

   // Follows the tables: call after one of them moved or changed its size
   void updateGeometry();

   QRectF boundingRect() const override;
   void paint(QPainter*, const QStyleOptionGraphicsItem*, QWidget*) override;

protected:
   void mousePressEvent(QGraphicsSceneMouseEvent*) override;
   void mouseMoveEvent(QGraphicsSceneMouseEvent*) override;
   void mouseReleaseEvent(QGraphicsSceneMouseEvent*) override;

private:
   QColor color() const;

   TableGroupDTO m_group;
   QVector<TableItem*> m_tables;
   const DiagramStyle* m_style = nullptr;
   QRectF m_rect;
   QPointF m_lastDragPos;
   bool m_dragging = false;
   bool m_moved = false;
};
