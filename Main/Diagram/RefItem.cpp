#include "RefItem.h"
#include "TableItem.h"
#include "DiagramScene.h"
#include "DiagramStyle.h"
#include <QPainter>
#include <QPainterPathStroker>
#include <QGraphicsSceneMouseEvent>
#include <QVector2D>
#include <cmath>

namespace
{
   const qreal Stub = 24;          // the straight part out of a table
   const qreal CornerRadius = 8;
   const qreal EndMargin = 18;     // room for the cardinality marks

   QPointF unit(const QPointF& from, const QPointF& to)
   {
      const QVector2D vector(to - from);
      return vector.length() < 1e-6 ? QPointF() : (vector / vector.length()).toPointF();
   }

   qreal distance(const QPointF& a, const QPointF& b)
   {
      return QVector2D(b - a).length();
   }

   // An orthogonal polyline with rounded corners
   QPainterPath roundedPath(QVector<QPointF> points)
   {
      // drop repeated and collinear points
      QVector<QPointF> clean;
      for (const QPointF& point : points)
      {
         if (!clean.isEmpty() && distance(clean.last(), point) < 0.5)
         {
            continue;
         }
         if (clean.size() >= 2)
         {
            const QPointF a = clean[clean.size() - 2];
            const QPointF b = clean.last();
            const bool collinear = (std::abs(a.x() - b.x()) < 0.5 && std::abs(b.x() - point.x()) < 0.5)
                  || (std::abs(a.y() - b.y()) < 0.5 && std::abs(b.y() - point.y()) < 0.5);
            if (collinear)
            {
               clean.last() = point;
               continue;
            }
         }
         clean << point;
      }

      QPainterPath path;
      if (clean.isEmpty())
      {
         return path;
      }
      path.moveTo(clean.first());
      for (int i = 1; i < clean.size() - 1; ++i)
      {
         const QPointF corner = clean[i];
         const qreal radius = std::min({CornerRadius, distance(clean[i - 1], corner) / 2, distance(corner, clean[i + 1]) / 2});
         path.lineTo(corner - unit(clean[i - 1], corner) * radius);
         path.quadTo(corner, corner + unit(corner, clean[i + 1]) * radius);
      }
      path.lineTo(clean.last());
      return path;
   }
}

RefItem::RefItem(const RefDTO &ref, TableItem *left, TableItem *right, const DiagramStyle *style)
   : m_ref(ref), m_left(left), m_right(right), m_style(style)
{
   setFlags(ItemIsSelectable);
   setAcceptHoverEvents(true);
   setZValue(5);
   setToolTip(tooltip());
   updatePath();
}

int RefItem::type() const
{
   return Type;
}

const RefDTO& RefItem::ref() const
{
   return m_ref;
}

TableItem* RefItem::leftTable() const
{
   return m_left;
}

TableItem* RefItem::rightTable() const
{
   return m_right;
}

void RefItem::updatePath()
{
   prepareGeometryChange();
   const QString leftColumn = m_ref.left.columns.value(0);
   const QString rightColumn = m_ref.right.columns.value(0);
   const QRectF a = m_left->sceneCardRect();
   const QRectF b = m_right->sceneCardRect();

   bool leftOnRight = true;  // the line leaves the left table on its right edge
   bool rightOnRight = false;
   if (m_left == m_right)
   {
      rightOnRight = true;
   }
   else if (a.right() + Stub * 2 <= b.left())
   {
      leftOnRight = true;
      rightOnRight = false;
   }
   else if (b.right() + Stub * 2 <= a.left())
   {
      leftOnRight = false;
      rightOnRight = true;
   }
   else
   {
      // the tables overlap horizontally: go around on the side where the detour is shorter
      const qreal outerRight = std::max(a.right(), b.right()) + Stub;
      const qreal outerLeft = std::min(a.left(), b.left()) - Stub;
      const qreal costRight = (outerRight - a.right()) + (outerRight - b.right());
      const qreal costLeft = (a.left() - outerLeft) + (b.left() - outerLeft);
      leftOnRight = rightOnRight = costRight <= costLeft;
   }

   m_leftPoint = m_left->anchor(leftColumn, leftOnRight);
   m_rightPoint = m_right->anchor(rightColumn, rightOnRight);
   m_leftDirection = QPointF(leftOnRight ? 1 : -1, 0);
   m_rightDirection = QPointF(rightOnRight ? 1 : -1, 0);

   QVector<QPointF> points{m_leftPoint};
   if (leftOnRight != rightOnRight)
   {
      const qreal middle = (m_leftPoint.x() + m_rightPoint.x()) / 2;
      points << QPointF(middle, m_leftPoint.y()) << QPointF(middle, m_rightPoint.y());
   }
   else
   {
      qreal x = leftOnRight ? std::max(a.right(), b.right()) + Stub + 8 : std::min(a.left(), b.left()) - Stub - 8;
      if (m_left == m_right && std::abs(m_leftPoint.y() - m_rightPoint.y()) < 1)
      {
         // a column referencing itself: a small loop
         points << QPointF(x, m_leftPoint.y()) << QPointF(x, m_leftPoint.y() + 14) << QPointF(m_rightPoint.x() + Stub / 2, m_leftPoint.y() + 14)
                << QPointF(m_rightPoint.x() + Stub / 2, m_rightPoint.y());
         m_path = roundedPath(points);
         return;
      }
      points << QPointF(x, m_leftPoint.y()) << QPointF(x, m_rightPoint.y());
   }
   points << m_rightPoint;
   m_path = roundedPath(points);
}

void RefItem::setHighlighted(bool highlighted)
{
   if (m_highlighted != highlighted)
   {
      m_highlighted = highlighted;
      setZValue(highlighted ? 6 : 5);
      update();
   }
}

QRectF RefItem::boundingRect() const
{
   return m_path.boundingRect().adjusted(-EndMargin, -EndMargin, EndMargin, EndMargin);
}

QPainterPath RefItem::shape() const
{
   QPainterPathStroker stroker;
   stroker.setWidth(10);
   return stroker.createStroke(m_path);
}

void RefItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *, QWidget *)
{
   const Theme& theme = m_style->theme;
   const bool active = !m_style->exporting && (m_highlighted || isSelected());
   QColor color = active ? theme.refHighlight : (QColor(m_ref.color).isValid() ? QColor(m_ref.color) : theme.refLine);

   painter->setRenderHint(QPainter::Antialiasing);
   QPen pen(color, active ? 2.4 : 1.5);
   pen.setCapStyle(Qt::RoundCap);
   pen.setJoinStyle(Qt::RoundJoin);
   painter->setPen(pen);
   painter->setBrush(Qt::NoBrush);
   painter->drawPath(m_path);

   drawEnd(painter, m_leftPoint, m_leftDirection, m_ref.leftCardinality, color);
   drawEnd(painter, m_rightPoint, m_rightDirection, m_ref.rightCardinality, color);
}

void RefItem::drawEnd(QPainter *painter, const QPointF &point, const QPointF &direction, Cardinality cardinality, const QColor &color) const
{
   const QPointF normal(-direction.y(), direction.x());
   if (m_style->notation == DiagramStyle::Notation::CrowsFoot)
   {
      painter->setPen(QPen(color, 1.5));
      if (cardinality == Cardinality::Many)
      {
         const QPointF tip = point + direction * 12;
         painter->drawLine(tip, point + normal * 6);
         painter->drawLine(tip, point - normal * 6);
         painter->drawLine(tip, point);
      }
      else
      {
         const QPointF bar = point + direction * 8;
         painter->drawLine(bar + normal * 6, bar - normal * 6);
         const QPointF second = point + direction * 12;
         painter->drawLine(second + normal * 6, second - normal * 6);
      }
      return;
   }

   QFont font = m_style->typeFont;
   font.setBold(true);
   painter->setFont(font);
   painter->setPen(color);
   const QString label = cardinality == Cardinality::Many ? QStringLiteral("*") : QStringLiteral("1");
   const QPointF center = point + direction * 10 + QPointF(0, -9);
   painter->drawText(QRectF(center.x() - 8, center.y() - 8, 16, 16), Qt::AlignCenter, label);
}

QString RefItem::tooltip() const
{
   auto endpoint = [](const RefEndpointDTO& endpoint) {
      const QString table = (endpoint.schema.isEmpty() || endpoint.schema == QLatin1String("public")) ? endpoint.table : endpoint.schema + '.' + endpoint.table;
      const QString columns = endpoint.columns.size() == 1 ? endpoint.columns.first() : '(' + endpoint.columns.join(QStringLiteral(", ")) + ')';
      return table + '.' + columns;
   };
   QString relation;
   const QString symbol = m_ref.relationSymbol();
   if (symbol == '>')
   {
      relation = QStringLiteral("many-to-one");
   }
   else if (symbol == '<')
   {
      relation = QStringLiteral("one-to-many");
   }
   else if (symbol == '-')
   {
      relation = QStringLiteral("one-to-one");
   }
   else
   {
      relation = QStringLiteral("many-to-many");
   }
   QString text = QStringLiteral("<b>%1 %2 %3</b><br>%4").arg(endpoint(m_ref.left).toHtmlEscaped(), symbol.toHtmlEscaped(), endpoint(m_ref.right).toHtmlEscaped(), relation);
   if (!m_ref.name.isEmpty())
   {
      text += QStringLiteral("<br>name: ") + m_ref.name.toHtmlEscaped();
   }
   if (!m_ref.onDelete.isEmpty())
   {
      text += QStringLiteral("<br>on delete: ") + m_ref.onDelete;
   }
   if (!m_ref.onUpdate.isEmpty())
   {
      text += QStringLiteral("<br>on update: ") + m_ref.onUpdate;
   }
   return text;
}

void RefItem::hoverEnterEvent(QGraphicsSceneHoverEvent *event)
{
   static_cast<DiagramScene*>(scene())->setHoveredRef(this);
   QGraphicsItem::hoverEnterEvent(event);
}

void RefItem::hoverLeaveEvent(QGraphicsSceneHoverEvent *event)
{
   static_cast<DiagramScene*>(scene())->setHoveredRef(nullptr);
   QGraphicsItem::hoverLeaveEvent(event);
}

void RefItem::mousePressEvent(QGraphicsSceneMouseEvent *event)
{
   QGraphicsItem::mousePressEvent(event);
   if (event->button() == Qt::LeftButton)
   {
      static_cast<DiagramScene*>(scene())->activate(m_ref.line);
   }
}
