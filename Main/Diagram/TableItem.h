#pragma once
#include <QGraphicsItem>
#include <QSet>
#include "DTO/TableDTO.h"
#include "ItemType.h"
struct DiagramStyle;
class DiagramScene;

// A table card: a colored header with the name and a row per visible column
class TableItem : public QGraphicsItem
{
public:
   enum { Type = ItemType::Table };

   // relatedColumns: the columns taking part in refs, shown in the KeysOnly detail
   // enumTypes: column types that name enums, drawn as links
   TableItem(const TableDTO&, const QSet<QString>& relatedColumns, const QSet<QString>& enumTypes, const DiagramStyle*);

   int type() const override;
   const TableDTO& table() const;
   QString fullName() const;

   // Recomputes the size after a change of the style
   void relayout();
   QSizeF size() const;

   // Where a ref line meets the column row: on the left or the right edge, in scene coordinates.
   // A hidden column gives the middle of the header
   QPointF anchor(const QString& column, bool rightSide) const;
   QRectF sceneCardRect() const;

   void setHeaderColorOverride(const QColor&);
   QColor headerColor() const;

   void setHighlightedColumns(const QSet<QString>&);
   void setHighlighted(bool);

   QRectF boundingRect() const override;
   QPainterPath shape() const override;
   void paint(QPainter*, const QStyleOptionGraphicsItem*, QWidget*) override;

protected:
   QVariant itemChange(GraphicsItemChange, const QVariant&) override;
   void hoverMoveEvent(QGraphicsSceneHoverEvent*) override;
   void hoverLeaveEvent(QGraphicsSceneHoverEvent*) override;
   void mousePressEvent(QGraphicsSceneMouseEvent*) override;
   void mouseReleaseEvent(QGraphicsSceneMouseEvent*) override;
   void mouseDoubleClickEvent(QGraphicsSceneMouseEvent*) override;

private:
   DiagramScene* diagramScene() const;
   // -1 for the header, the visible row index otherwise, -2 outside
   int rowAt(const QPointF& localPos) const;
   QString columnTooltip(const ColumnDTO&) const;
   QString headerTooltip() const;
   void drawKey(QPainter*, const QRectF& area) const;

   TableDTO m_table;
   QSet<QString> m_relatedColumns;
   QSet<QString> m_enumTypes;
   const DiagramStyle* m_style = nullptr;
   QVector<int> m_visibleColumns;
   QColor m_headerColorOverride;
   qreal m_width = 200;
   qreal m_height = 100;
   int m_hoveredRow = -2;
   QSet<QString> m_highlightedColumns;
   bool m_highlighted = false;
   bool m_moved = false;
};
