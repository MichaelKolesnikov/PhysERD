#include "TableItem.h"
#include "DiagramScene.h"
#include "DiagramStyle.h"
#include <QPainter>
#include <QGraphicsSceneHoverEvent>
#include <QGraphicsSceneMouseEvent>
#include <QStyleOptionGraphicsItem>
#include <QFontMetricsF>
#include <cmath>

namespace
{
   const qreal Radius = 6;
   const qreal Padding = 12;
   const qreal IconWidth = 18;
   const qreal ShadowOffset = 3;

   QString escapeHtml(const QString& text)
   {
      return text.toHtmlEscaped().replace('\n', QStringLiteral("<br>"));
   }
}

TableItem::TableItem(const TableDTO &table, const QSet<QString> &relatedColumns, const QSet<QString> &enumTypes, const DiagramStyle *style)
   : m_table(table), m_relatedColumns(relatedColumns), m_enumTypes(enumTypes), m_style(style)
{
   setFlags(ItemIsSelectable | ItemIsMovable | ItemSendsGeometryChanges);
   setAcceptHoverEvents(true);
   setZValue(10);
   relayout();
}

int TableItem::type() const
{
   return Type;
}

const TableDTO& TableItem::table() const
{
   return m_table;
}

QString TableItem::fullName() const
{
   return m_table.fullName();
}

void TableItem::relayout()
{
   prepareGeometryChange();
   m_visibleColumns.clear();
   for (int i = 0; i < m_table.columns.size(); ++i)
   {
      const ColumnDTO& column = m_table.columns[i];
      switch (m_style->detail)
      {
      case DiagramStyle::Detail::AllFields:
         m_visibleColumns << i;
         break;
      case DiagramStyle::Detail::KeysOnly:
         if (column.pk || m_relatedColumns.contains(column.name))
         {
            m_visibleColumns << i;
         }
         break;
      case DiagramStyle::Detail::TableNames:
         break;
      }
   }

   const QFontMetricsF header(m_style->headerFont);
   const QFontMetricsF name(m_style->columnFont);
   const QFontMetricsF type(m_style->typeFont);
   qreal width = std::max(m_style->minTableWidth, header.horizontalAdvance(m_table.displayName()) + Padding * 2 + 20);
   for (const ColumnDTO& column : qAsConst(m_table.columns))
   {
      const qreal rowWidth = Padding + IconWidth + name.horizontalAdvance(column.name) + 24 + type.horizontalAdvance(column.type) + IconWidth + Padding;
      width = std::max(width, rowWidth);
   }
   m_width = std::ceil(width);
   m_height = m_style->headerHeight + m_visibleColumns.size() * m_style->rowHeight + (m_visibleColumns.isEmpty() ? 0 : 4);
   update();
}

QSizeF TableItem::size() const
{
   return QSizeF(m_width, m_height);
}

QPointF TableItem::anchor(const QString &column, bool rightSide) const
{
   qreal y = m_style->headerHeight / 2;
   const int index = m_table.columnIndex(column);
   const int row = m_visibleColumns.indexOf(index);
   if (row >= 0)
   {
      y = m_style->headerHeight + row * m_style->rowHeight + m_style->rowHeight / 2;
   }
   return mapToScene(QPointF(rightSide ? m_width : 0, y));
}

QRectF TableItem::sceneCardRect() const
{
   return QRectF(pos(), size());
}

void TableItem::setHeaderColorOverride(const QColor &color)
{
   m_headerColorOverride = color;
   update();
}

QColor TableItem::headerColor() const
{
   if (m_headerColorOverride.isValid())
   {
      return m_headerColorOverride;
   }
   const QColor color(m_table.headerColor);
   return color.isValid() ? color : m_style->theme.defaultHeader;
}

void TableItem::setHighlightedColumns(const QSet<QString> &columns)
{
   if (m_highlightedColumns != columns)
   {
      m_highlightedColumns = columns;
      update();
   }
}

void TableItem::setHighlighted(bool highlighted)
{
   if (m_highlighted != highlighted)
   {
      m_highlighted = highlighted;
      update();
   }
}

QRectF TableItem::boundingRect() const
{
   return QRectF(-2, -2, m_width + 4 + ShadowOffset, m_height + 4 + ShadowOffset);
}

QPainterPath TableItem::shape() const
{
   QPainterPath path;
   path.addRoundedRect(QRectF(0, 0, m_width, m_height), Radius, Radius);
   return path;
}

void TableItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *, QWidget *)
{
   const Theme& theme = m_style->theme;
   const QRectF card(0, 0, m_width, m_height);
   painter->setRenderHint(QPainter::Antialiasing);

   // shadow
   painter->setPen(Qt::NoPen);
   painter->setBrush(theme.tableShadow);
   painter->drawRoundedRect(card.translated(ShadowOffset / 2, ShadowOffset), Radius, Radius);

   // body
   painter->setBrush(theme.tableBody);
   painter->drawRoundedRect(card, Radius, Radius);

   // header with rounded top corners only
   const QColor header = headerColor();
   QPainterPath headerPath;
   headerPath.addRoundedRect(QRectF(0, 0, m_width, m_style->headerHeight), Radius, Radius);
   QPainterPath bottomHalf;
   bottomHalf.addRect(QRectF(0, m_style->headerHeight / 2, m_width, m_style->headerHeight / 2));
   painter->setBrush(header);
   painter->drawPath(headerPath.united(bottomHalf));

   if (m_hoveredRow == -1 && !m_style->exporting)
   {
      painter->setBrush(QColor(255, 255, 255, 30));
      painter->drawPath(headerPath.united(bottomHalf));
   }

   painter->setFont(m_style->headerFont);
   painter->setPen(Theme::textOn(header));
   const QRectF titleRect(Padding, 0, m_width - Padding * 2, m_style->headerHeight);
   painter->drawText(titleRect, Qt::AlignVCenter | Qt::AlignLeft, QFontMetricsF(m_style->headerFont).elidedText(m_table.displayName(), Qt::ElideRight, titleRect.width() - 16));
   if (!m_table.note.isEmpty())
   {
      // a small speech mark tells the table has a note
      const QRectF mark(m_width - Padding - 10, m_style->headerHeight / 2 - 5, 10, 9);
      QColor markColor = Theme::textOn(header);
      markColor.setAlpha(170);
      painter->setPen(QPen(markColor, 1.2));
      painter->setBrush(Qt::NoBrush);
      painter->drawRoundedRect(mark, 2, 2);
   }

   // rows
   const QFontMetricsF typeMetrics(m_style->typeFont);
   for (int row = 0; row < m_visibleColumns.size(); ++row)
   {
      const ColumnDTO& column = m_table.columns[m_visibleColumns[row]];
      const QRectF rowRect(0, m_style->headerHeight + row * m_style->rowHeight, m_width, m_style->rowHeight);

      const bool highlighted = m_highlightedColumns.contains(column.name);
      if (!m_style->exporting && (highlighted || row == m_hoveredRow))
      {
         painter->setPen(Qt::NoPen);
         painter->setBrush(highlighted ? theme.rowHighlight : theme.rowHover);
         painter->drawRect(rowRect.adjusted(1, 0, -1, 0));
      }
      if (row > 0)
      {
         painter->setPen(QPen(theme.rowSeparator, 1));
         painter->drawLine(QPointF(1, rowRect.top()), QPointF(m_width - 1, rowRect.top()));
      }

      if (column.pk)
      {
         drawKey(painter, QRectF(Padding - 2, rowRect.top(), IconWidth, rowRect.height()));
      }

      QFont nameFont = m_style->columnFont;
      nameFont.setBold(column.pk);
      painter->setFont(nameFont);
      painter->setPen(theme.columnText);
      const qreal typeWidth = typeMetrics.horizontalAdvance(column.type);
      const QRectF nameRect(Padding + IconWidth, rowRect.top(), m_width - Padding * 2 - IconWidth - typeWidth - 12 - IconWidth, rowRect.height());
      QString name = column.name;
      if (column.notNull && !column.pk)
      {
         name += QStringLiteral(" *");
      }
      painter->drawText(nameRect, Qt::AlignVCenter | Qt::AlignLeft, QFontMetricsF(nameFont).elidedText(name, Qt::ElideRight, nameRect.width()));

      QFont typeFont = m_style->typeFont;
      const bool isEnum = m_enumTypes.contains(column.type);
      typeFont.setUnderline(isEnum);
      painter->setFont(typeFont);
      painter->setPen(isEnum ? theme.refHighlight : theme.typeText);
      const QRectF typeRect(0, rowRect.top(), m_width - Padding - IconWidth + 4, rowRect.height());
      painter->drawText(typeRect, Qt::AlignVCenter | Qt::AlignRight, column.type);

      if (!column.note.isEmpty())
      {
         painter->setPen(Qt::NoPen);
         painter->setBrush(theme.noteMark);
         painter->drawEllipse(QPointF(m_width - Padding + 1, rowRect.center().y()), 2.5, 2.5);
      }
   }

   // border
   painter->setBrush(Qt::NoBrush);
   if (!m_style->exporting && (isSelected() || m_highlighted))
   {
      painter->setPen(QPen(theme.selection, isSelected() ? 2.2 : 1.6));
   }
   else
   {
      painter->setPen(QPen(theme.tableBorder, 1));
   }
   painter->drawRoundedRect(card, Radius, Radius);
}

void TableItem::drawKey(QPainter *painter, const QRectF &area) const
{
   const QPointF center = area.center();
   QPen pen(m_style->theme.keyIcon, 1.6);
   pen.setCapStyle(Qt::RoundCap);
   painter->setPen(pen);
   painter->setBrush(Qt::NoBrush);
   const QPointF ring(center.x() - 3.5, center.y());
   painter->drawEllipse(ring, 3, 3);
   painter->drawLine(QPointF(ring.x() + 3, center.y()), QPointF(center.x() + 5.5, center.y()));
   painter->drawLine(QPointF(center.x() + 3.5, center.y()), QPointF(center.x() + 3.5, center.y() + 2.5));
   painter->drawLine(QPointF(center.x() + 5.5, center.y()), QPointF(center.x() + 5.5, center.y() + 3));
}

DiagramScene* TableItem::diagramScene() const
{
   return static_cast<DiagramScene*>(scene());
}

int TableItem::rowAt(const QPointF &localPos) const
{
   if (localPos.x() < 0 || localPos.x() > m_width || localPos.y() < 0 || localPos.y() > m_height)
   {
      return -2;
   }
   if (localPos.y() < m_style->headerHeight)
   {
      return -1;
   }
   const int row = int((localPos.y() - m_style->headerHeight) / m_style->rowHeight);
   return row < m_visibleColumns.size() ? row : -2;
}

QString TableItem::columnTooltip(const ColumnDTO &column) const
{
   QStringList flags;
   if (column.pk)
   {
      flags << QStringLiteral("primary key");
   }
   if (column.increment)
   {
      flags << QStringLiteral("increment");
   }
   if (column.notNull)
   {
      flags << QStringLiteral("not null");
   }
   if (column.unique)
   {
      flags << QStringLiteral("unique");
   }
   QString text = QStringLiteral("<b>%1</b> <span style='color:gray'>%2</span>").arg(escapeHtml(column.name), escapeHtml(column.type));
   if (!flags.isEmpty())
   {
      text += QStringLiteral("<br>") + flags.join(QStringLiteral(", "));
   }
   if (column.defaultKind != ColumnDTO::NoDefault)
   {
      text += QStringLiteral("<br>default: ") + escapeHtml(column.defaultValue);
   }
   if (!column.note.isEmpty())
   {
      text += QStringLiteral("<hr>") + escapeHtml(column.note);
   }
   return text;
}

QString TableItem::headerTooltip() const
{
   QString text = QStringLiteral("<b>%1</b>").arg(escapeHtml(m_table.displayName()));
   if (!m_table.alias.isEmpty())
   {
      text += QStringLiteral(" as ") + escapeHtml(m_table.alias);
   }
   text += QStringLiteral("<br>%1 columns").arg(m_table.columns.size());
   if (!m_table.indexes.isEmpty())
   {
      text += QStringLiteral(", %1 indexes").arg(m_table.indexes.size());
   }
   if (!m_table.note.isEmpty())
   {
      text += QStringLiteral("<hr>") + escapeHtml(m_table.note);
   }
   return text;
}

QVariant TableItem::itemChange(GraphicsItemChange change, const QVariant &value)
{
   if (change == ItemPositionChange && m_style->snapToGrid)
   {
      const QPointF point = value.toPointF();
      const qreal grid = m_style->gridSize;
      return QPointF(std::round(point.x() / grid) * grid, std::round(point.y() / grid) * grid);
   }
   if (change == ItemPositionHasChanged && scene())
   {
      m_moved = true;
      diagramScene()->tableMoved(this);
   }
   if (change == ItemSelectedHasChanged)
   {
      setZValue(value.toBool() ? 11 : 10);
   }
   return QGraphicsItem::itemChange(change, value);
}

void TableItem::hoverMoveEvent(QGraphicsSceneHoverEvent *event)
{
   const int row = rowAt(event->pos());
   if (row == m_hoveredRow)
   {
      return;
   }
   m_hoveredRow = row;
   update();
   if (row == -1)
   {
      setToolTip(headerTooltip());
      diagramScene()->setHoveredColumn(this, QString());
   }
   else if (row >= 0)
   {
      const ColumnDTO& column = m_table.columns[m_visibleColumns[row]];
      setToolTip(columnTooltip(column));
      diagramScene()->setHoveredColumn(this, column.name);
   }
   else
   {
      setToolTip(QString());
      diagramScene()->setHoveredColumn(nullptr, QString());
   }
}

void TableItem::hoverLeaveEvent(QGraphicsSceneHoverEvent *event)
{
   m_hoveredRow = -2;
   update();
   diagramScene()->setHoveredColumn(nullptr, QString());
   QGraphicsItem::hoverLeaveEvent(event);
}

void TableItem::mousePressEvent(QGraphicsSceneMouseEvent *event)
{
   m_moved = false;
   QGraphicsItem::mousePressEvent(event);
}

void TableItem::mouseReleaseEvent(QGraphicsSceneMouseEvent *event)
{
   QGraphicsItem::mouseReleaseEvent(event);
   if (event->button() != Qt::LeftButton)
   {
      return;
   }
   if (m_moved)
   {
      diagramScene()->moveFinished();
      return;
   }
   // a click without a drag shows the definition in the editor
   const int row = rowAt(event->pos());
   const int line = row >= 0 ? m_table.columns[m_visibleColumns[row]].line : m_table.line;
   diagramScene()->activate(line);
}

void TableItem::mouseDoubleClickEvent(QGraphicsSceneMouseEvent *event)
{
   const int row = rowAt(event->pos());
   if (row >= 0)
   {
      const ColumnDTO& column = m_table.columns[m_visibleColumns[row]];
      if (m_enumTypes.contains(column.type))
      {
         diagramScene()->activateEnum(column.type);
         return;
      }
   }
   QGraphicsItem::mouseDoubleClickEvent(event);
}
