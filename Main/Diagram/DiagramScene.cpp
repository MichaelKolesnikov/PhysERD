#include "DiagramScene.h"
#include "TableItem.h"
#include "RefItem.h"
#include "TableGroupItem.h"
#include "StickyNoteItem.h"
#include "Layout/AutoLayout.h"
#include <QPainter>
#include <QUndoStack>
#include <QMenu>
#include <QColorDialog>
#include <QGraphicsSceneContextMenuEvent>
#include <QGraphicsView>
#include <QHash>
#include <cmath>

namespace
{
   const qreal NewTableGap = 80;
   const qreal SceneMargin = 2000;

   // Restores the positions of the tables and notes before a move or an arrangement
   class LayoutCommand : public QUndoCommand
   {
   public:
      LayoutCommand(DiagramScene* scene, const LayoutDTO& before, const LayoutDTO& after, const QString& text)
         : QUndoCommand(text), m_scene(scene), m_before(before), m_after(after)
      {

      }

      void undo() override
      {
         m_scene->setLayout(m_before);
         emit m_scene->layoutChanged();
      }

      void redo() override
      {
         if (m_first)
         {
            m_first = false; // the scene is already there
            return;
         }
         m_scene->setLayout(m_after);
         emit m_scene->layoutChanged();
      }

   private:
      DiagramScene* m_scene;
      LayoutDTO m_before;
      LayoutDTO m_after;
      bool m_first = true;
   };
}

struct DiagramScene::Impl
{
   explicit Impl(DiagramScene* scene) : scene(scene) {}

   void clearItems();
   void buildItems();
   void placeTables();
   void storePositions();
   void updateAll();
   void updateSceneRect(bool allowShrink);
   void clearHover();
   void highlightRef(RefItem*);
   void relayoutAll();
   void pushLayoutCommand(const QString& text);

   DiagramScene* scene = nullptr;
   SchemaDTO schema;
   DiagramStyle style;
   LayoutDTO layout;          // positions of everything ever seen, so an element keeps its place after a typo
   LayoutDTO settledLayout;   // after the last finished move: the "before" of the next undo step
   QUndoStack* undoStack = nullptr;

   QVector<TableItem*> tables;
   QHash<QString, TableItem*> tableByName;
   QVector<RefItem*> refs;
   QHash<TableItem*, QVector<RefItem*>> refsOfTable;
   QVector<TableGroupItem*> groups;
   QHash<TableItem*, TableGroupItem*> groupOfTable;
   QVector<StickyNoteItem*> notes;

   QVector<RefItem*> highlightedRefs;
   QVector<TableItem*> highlightedTables;
};

DiagramScene::DiagramScene(QObject *parent)
   : QGraphicsScene(parent)
{
   pimpl = std::make_unique<Impl>(this);
   pimpl->undoStack = new QUndoStack(this);
   QFont font = QFont();
   font.setPointSizeF(10);
   setDiagramFont(font);
   setSceneRect(-SceneMargin, -SceneMargin, SceneMargin * 2, SceneMargin * 2);
}

DiagramScene::~DiagramScene() = default;

void DiagramScene::Impl::clearItems()
{
   highlightedRefs.clear();
   highlightedTables.clear();
   for (RefItem* ref : qAsConst(refs))
   {
      delete ref;
   }
   for (TableGroupItem* group : qAsConst(groups))
   {
      delete group;
   }
   for (TableItem* table : qAsConst(tables))
   {
      delete table;
   }
   for (StickyNoteItem* note : qAsConst(notes))
   {
      delete note;
   }
   tables.clear();
   tableByName.clear();
   refs.clear();
   refsOfTable.clear();
   groups.clear();
   groupOfTable.clear();
   notes.clear();
}

void DiagramScene::Impl::buildItems()
{
   QHash<QString, QSet<QString>> relatedColumns;
   for (const RefDTO& ref : qAsConst(schema.refs))
   {
      for (const QString& column : ref.left.columns)
      {
         relatedColumns[ref.left.tableFullName()].insert(column);
      }
      for (const QString& column : ref.right.columns)
      {
         relatedColumns[ref.right.tableFullName()].insert(column);
      }
   }

   for (const TableDTO& table : qAsConst(schema.tables))
   {
      QSet<QString> enumTypes;
      for (const ColumnDTO& column : table.columns)
      {
         if (schema.enumForType(column.type))
         {
            enumTypes.insert(column.type);
         }
      }
      auto item = new TableItem(table, relatedColumns.value(table.fullName()), enumTypes, &style);
      const QColor color(layout.tableColors.value(table.fullName()));
      if (color.isValid())
      {
         item->setHeaderColorOverride(color);
      }
      tables << item;
      tableByName.insert(table.fullName(), item);
   }

   for (const RefDTO& ref : qAsConst(schema.refs))
   {
      TableItem* left = tableByName.value(ref.left.tableFullName());
      TableItem* right = tableByName.value(ref.right.tableFullName());
      if (!left || !right)
      {
         continue;
      }
      auto item = new RefItem(ref, left, right, &style);
      refs << item;
      refsOfTable[left] << item;
      if (right != left)
      {
         refsOfTable[right] << item;
      }
   }

   for (const TableGroupDTO& group : qAsConst(schema.tableGroups))
   {
      QVector<TableItem*> members;
      for (const QString& name : group.tables)
      {
         if (TableItem* table = tableByName.value(name))
         {
            members << table;
         }
      }
      if (members.isEmpty())
      {
         continue;
      }
      auto item = new TableGroupItem(group, members, &style);
      groups << item;
      for (TableItem* table : qAsConst(members))
      {
         groupOfTable.insert(table, item);
      }
   }

   for (const NoteDTO& note : qAsConst(schema.notes))
   {
      notes << new StickyNoteItem(note, &style);
   }
}

void DiagramScene::Impl::placeTables()
{
   QVector<TableItem*> unplaced;
   QRectF placedBounds;
   for (TableItem* table : qAsConst(tables))
   {
      if (layout.tables.contains(table->fullName()))
      {
         table->setPos(layout.tables.value(table->fullName()));
         placedBounds = placedBounds.isNull() ? table->sceneCardRect() : placedBounds.united(table->sceneCardRect());
      }
      else
      {
         unplaced << table;
      }
   }

   if (!unplaced.isEmpty())
   {
      if (placedBounds.isNull())
      {
         // nothing placed yet: arrange everything
         QMap<QString, QSizeF> sizes;
         for (TableItem* table : qAsConst(tables))
         {
            sizes.insert(table->fullName(), table->size());
         }
         const QMap<QString, QPointF> positions = AutoLayout::arrange(schema, sizes);
         for (TableItem* table : qAsConst(unplaced))
         {
            table->setPos(positions.value(table->fullName()));
         }
      }
      else
      {
         // a new table goes to the right of the others, the next one below it
         qreal y = placedBounds.top();
         const qreal x = placedBounds.right() + NewTableGap;
         for (TableItem* table : qAsConst(unplaced))
         {
            table->setPos(x, y);
            y += table->size().height() + NewTableGap / 2;
         }
      }
      for (TableItem* table : qAsConst(unplaced))
      {
         layout.tables.insert(table->fullName(), table->pos());
      }
   }

   // notes go above the tables if they have no place yet
   QRectF bounds;
   for (TableItem* table : qAsConst(tables))
   {
      bounds = bounds.isNull() ? table->sceneCardRect() : bounds.united(table->sceneCardRect());
   }
   qreal noteX = bounds.isNull() ? 0 : bounds.left();
   for (StickyNoteItem* note : qAsConst(notes))
   {
      const QString& name = note->note().name;
      if (layout.notes.contains(name))
      {
         note->setPos(layout.notes.value(name));
      }
      else
      {
         note->setPos(noteX, (bounds.isNull() ? 0 : bounds.top()) - note->boundingRect().height() - NewTableGap);
         noteX += note->boundingRect().width() + 30;
         layout.notes.insert(name, note->pos());
      }
   }
}

void DiagramScene::Impl::storePositions()
{
   for (TableItem* table : qAsConst(tables))
   {
      layout.tables.insert(table->fullName(), table->pos());
   }
   for (StickyNoteItem* note : qAsConst(notes))
   {
      layout.notes.insert(note->note().name, note->pos());
   }
}

void DiagramScene::Impl::updateAll()
{
   for (RefItem* ref : qAsConst(refs))
   {
      ref->updatePath();
   }
   for (TableGroupItem* group : qAsConst(groups))
   {
      group->updateGeometry();
   }
}

void DiagramScene::Impl::updateSceneRect(bool allowShrink)
{
   const QRectF content = scene->contentRect();
   QRectF wanted = content.isNull() ? QRectF(-SceneMargin, -SceneMargin, SceneMargin * 2, SceneMargin * 2)
                                    : content.adjusted(-SceneMargin, -SceneMargin, SceneMargin, SceneMargin);
   if (!allowShrink)
   {
      wanted = wanted.united(scene->sceneRect());
   }
   if (wanted != scene->sceneRect())
   {
      scene->setSceneRect(wanted);
   }
}

void DiagramScene::Impl::relayoutAll()
{
   for (TableItem* table : qAsConst(tables))
   {
      table->relayout();
   }
   for (StickyNoteItem* note : qAsConst(notes))
   {
      note->relayout();
   }
   updateAll();
   scene->update();
}

void DiagramScene::Impl::pushLayoutCommand(const QString& text)
{
   storePositions();
   const LayoutDTO after = scene->layout();
   if (after.tables == settledLayout.tables && after.notes == settledLayout.notes && after.tableColors == settledLayout.tableColors)
   {
      return;
   }
   undoStack->push(new LayoutCommand(scene, settledLayout, after, text));
   settledLayout = after;
}

void DiagramScene::setSchema(const SchemaDTO &schema)
{
   pimpl->storePositions();
   pimpl->clearItems();
   pimpl->schema = schema;
   pimpl->buildItems();
   pimpl->placeTables();
   for (TableGroupItem* group : qAsConst(pimpl->groups))
   {
      addItem(group);
   }
   for (TableItem* table : qAsConst(pimpl->tables))
   {
      addItem(table);
   }
   for (RefItem* ref : qAsConst(pimpl->refs))
   {
      addItem(ref);
   }
   for (StickyNoteItem* note : qAsConst(pimpl->notes))
   {
      addItem(note);
   }
   pimpl->updateAll();
   pimpl->settledLayout = layout();
   pimpl->updateSceneRect(false);
}

const SchemaDTO& DiagramScene::schema() const
{
   return pimpl->schema;
}

bool DiagramScene::hasTables() const
{
   return !pimpl->tables.isEmpty();
}

LayoutDTO DiagramScene::layout() const
{
   // only what is on the diagram now
   LayoutDTO result;
   for (TableItem* table : qAsConst(pimpl->tables))
   {
      result.tables.insert(table->fullName(), table->pos());
      if (pimpl->layout.tableColors.contains(table->fullName()))
      {
         result.tableColors.insert(table->fullName(), pimpl->layout.tableColors.value(table->fullName()));
      }
   }
   for (StickyNoteItem* note : qAsConst(pimpl->notes))
   {
      result.notes.insert(note->note().name, note->pos());
   }
   return result;
}

void DiagramScene::setLayout(const LayoutDTO &layout)
{
   // without items the rebuild has no positions to keep: the given ones win
   pimpl->clearItems();
   pimpl->layout = layout;
   setSchema(SchemaDTO(pimpl->schema));
}

void DiagramScene::autoArrange()
{
   pimpl->storePositions();
   QMap<QString, QSizeF> sizes;
   for (TableItem* table : qAsConst(pimpl->tables))
   {
      sizes.insert(table->fullName(), table->size());
   }
   const QMap<QString, QPointF> positions = AutoLayout::arrange(pimpl->schema, sizes);
   for (TableItem* table : qAsConst(pimpl->tables))
   {
      table->setPos(positions.value(table->fullName()));
   }

   // the notes go above the tables in a row
   QRectF bounds;
   for (TableItem* table : qAsConst(pimpl->tables))
   {
      bounds = bounds.isNull() ? table->sceneCardRect() : bounds.united(table->sceneCardRect());
   }
   qreal x = bounds.isNull() ? 0 : bounds.left();
   for (StickyNoteItem* note : qAsConst(pimpl->notes))
   {
      note->setPos(x, (bounds.isNull() ? 0 : bounds.top()) - note->boundingRect().height() - NewTableGap);
      x += note->boundingRect().width() + 30;
   }
   pimpl->updateAll();
   pimpl->updateSceneRect(true);
   pimpl->pushLayoutCommand(tr("Auto-arrange"));
   emit layoutChanged();
}

const DiagramStyle& DiagramScene::style() const
{
   return pimpl->style;
}

void DiagramScene::setTheme(const Theme &theme)
{
   pimpl->style.theme = theme;
   pimpl->relayoutAll();
}

void DiagramScene::setDetail(DiagramStyle::Detail detail)
{
   pimpl->style.detail = detail;
   pimpl->relayoutAll();
}

void DiagramScene::setNotation(DiagramStyle::Notation notation)
{
   pimpl->style.notation = notation;
   pimpl->relayoutAll();
}

void DiagramScene::setSnapToGrid(bool snap)
{
   pimpl->style.snapToGrid = snap;
}

void DiagramScene::setDiagramFont(const QFont &font)
{
   pimpl->style.columnFont = font;
   pimpl->style.headerFont = font;
   pimpl->style.headerFont.setBold(true);
   pimpl->style.headerFont.setPointSizeF(font.pointSizeF() + 0.5);
   pimpl->style.typeFont = font;
   pimpl->style.typeFont.setPointSizeF(std::max(7.0, font.pointSizeF() - 0.5));
   const qreal lineHeight = QFontMetricsF(font).height();
   pimpl->style.rowHeight = std::ceil(lineHeight + 12);
   pimpl->style.headerHeight = std::ceil(lineHeight + 18);
   pimpl->relayoutAll();
}

void DiagramScene::setExporting(bool exporting)
{
   pimpl->style.exporting = exporting;
   if (exporting)
   {
      pimpl->clearHover();
   }
   update();
}

QUndoStack* DiagramScene::undoStack() const
{
   return pimpl->undoStack;
}

void DiagramScene::selectByLine(int line)
{
   QGraphicsItem* target = nullptr;
   for (TableItem* table : qAsConst(pimpl->tables))
   {
      if (line >= table->table().line && line <= table->table().endLine)
      {
         target = table;
         break;
      }
   }
   if (!target)
   {
      for (RefItem* ref : qAsConst(pimpl->refs))
      {
         if (!ref->ref().isInline && ref->ref().line == line)
         {
            target = ref;
            break;
         }
      }
   }
   if (target && target->isSelected() && selectedItems().size() == 1)
   {
      return;
   }
   clearSelection();
   if (!target)
   {
      return;
   }
   target->setSelected(true);
   for (QGraphicsView* view : views())
   {
      const QRectF visible = view->mapToScene(view->viewport()->rect()).boundingRect();
      if (!visible.intersects(target->sceneBoundingRect()))
      {
         view->centerOn(target);
      }
   }
}

QRectF DiagramScene::contentRect() const
{
   QRectF bounds;
   auto unite = [&](const QRectF& rect) {
      bounds = bounds.isNull() ? rect : bounds.united(rect);
   };
   for (TableItem* table : qAsConst(pimpl->tables))
   {
      unite(table->sceneBoundingRect());
   }
   for (TableGroupItem* group : qAsConst(pimpl->groups))
   {
      unite(group->sceneBoundingRect());
   }
   for (StickyNoteItem* note : qAsConst(pimpl->notes))
   {
      unite(note->sceneBoundingRect());
   }
   for (RefItem* ref : qAsConst(pimpl->refs))
   {
      unite(ref->sceneBoundingRect());
   }
   return bounds;
}

TableItem* DiagramScene::tableItem(const QString &fullName) const
{
   return pimpl->tableByName.value(fullName);
}

void DiagramScene::tableMoved(TableItem *table)
{
   for (RefItem* ref : pimpl->refsOfTable.value(table))
   {
      ref->updatePath();
   }
   if (TableGroupItem* group = pimpl->groupOfTable.value(table))
   {
      group->updateGeometry();
   }
}

void DiagramScene::moveFinished()
{
   pimpl->updateSceneRect(false);
   pimpl->pushLayoutCommand(tr("Move"));
   emit layoutChanged();
}

void DiagramScene::activate(int line)
{
   if (line > 0)
   {
      emit lineActivated(line);
   }
}

void DiagramScene::activateEnum(const QString &type)
{
   if (const EnumDTO* enumDTO = pimpl->schema.enumForType(type))
   {
      emit lineActivated(enumDTO->line);
   }
}

void DiagramScene::Impl::clearHover()
{
   for (RefItem* ref : qAsConst(highlightedRefs))
   {
      ref->setHighlighted(false);
   }
   for (TableItem* table : qAsConst(highlightedTables))
   {
      table->setHighlightedColumns({});
      table->setHighlighted(false);
   }
   highlightedRefs.clear();
   highlightedTables.clear();
}

void DiagramScene::Impl::highlightRef(RefItem *ref)
{
   ref->setHighlighted(true);
   highlightedRefs << ref;
   auto mark = [&](TableItem* table, const QStringList& columns) {
      QSet<QString> set;
      if (highlightedTables.contains(table))
      {
         // the table keeps the columns marked by the other refs
         for (RefItem* other : qAsConst(highlightedRefs))
         {
            if (other == ref)
            {
               continue;
            }
            if (other->leftTable() == table)
            {
               for (const QString& column : other->ref().left.columns)
               {
                  set.insert(column);
               }
            }
            if (other->rightTable() == table)
            {
               for (const QString& column : other->ref().right.columns)
               {
                  set.insert(column);
               }
            }
         }
      }
      for (const QString& column : columns)
      {
         set.insert(column);
      }
      table->setHighlightedColumns(set);
      table->setHighlighted(true);
      if (!highlightedTables.contains(table))
      {
         highlightedTables << table;
      }
   };
   mark(ref->leftTable(), ref->ref().left.columns);
   mark(ref->rightTable(), ref->ref().right.columns);
}

void DiagramScene::setHoveredColumn(TableItem *table, const QString &column)
{
   pimpl->clearHover();
   if (!table || pimpl->style.exporting)
   {
      return;
   }
   for (RefItem* ref : pimpl->refsOfTable.value(table))
   {
      const bool matches = column.isEmpty()
            || (ref->leftTable() == table && ref->ref().left.columns.contains(column))
            || (ref->rightTable() == table && ref->ref().right.columns.contains(column));
      if (matches)
      {
         pimpl->highlightRef(ref);
      }
   }
}

void DiagramScene::setHoveredRef(RefItem *ref)
{
   pimpl->clearHover();
   if (ref && !pimpl->style.exporting)
   {
      pimpl->highlightRef(ref);
   }
}

void DiagramScene::drawBackground(QPainter *painter, const QRectF &rect)
{
   const Theme& theme = pimpl->style.theme;
   painter->fillRect(rect, theme.canvas);
   if (pimpl->style.exporting)
   {
      return;
   }

   // dots on the grid; fewer of them when zoomed out
   const qreal scale = painter->worldTransform().m11();
   qreal step = pimpl->style.gridSize;
   while (step * scale < 12)
   {
      step *= 2;
   }
   const qreal left = std::floor(rect.left() / step) * step;
   const qreal top = std::floor(rect.top() / step) * step;
   QVector<QPointF> points;
   for (qreal x = left; x < rect.right(); x += step)
   {
      for (qreal y = top; y < rect.bottom(); y += step)
      {
         points << QPointF(x, y);
      }
   }
   QPen pen(theme.gridDot, 2 / std::max(scale, 0.01));
   pen.setCapStyle(Qt::RoundCap);
   painter->setPen(pen);
   painter->drawPoints(points.constData(), points.size());
}

void DiagramScene::contextMenuEvent(QGraphicsSceneContextMenuEvent *event)
{
   QGraphicsItem* item = itemAt(event->scenePos(), QTransform());
   while (item && item->type() != TableItem::Type && item->parentItem())
   {
      item = item->parentItem();
   }

   QMenu menu;
   if (item && item->type() == TableItem::Type)
   {
      auto table = static_cast<TableItem*>(item);
      const QString name = table->fullName();
      const int line = table->table().line;
      menu.addAction(tr("Go to Definition"), this, [this, line]() {emit lineActivated(line);});
      menu.addSeparator();
      menu.addAction(tr("Header Color..."), this, [this, name]() {
         TableItem* target = tableItem(name);
         if (!target)
         {
            return;
         }
         const QColor color = QColorDialog::getColor(target->headerColor(), nullptr, tr("Header Color"));
         if (!color.isValid())
         {
            return;
         }
         pimpl->layout.tableColors.insert(name, color.name());
         target->setHeaderColorOverride(color);
         pimpl->pushLayoutCommand(tr("Header Color"));
         emit layoutChanged();
      });
      QAction* reset = menu.addAction(tr("Reset Header Color"), this, [this, name]() {
         pimpl->layout.tableColors.remove(name);
         if (TableItem* target = tableItem(name))
         {
            target->setHeaderColorOverride(QColor());
         }
         pimpl->pushLayoutCommand(tr("Header Color"));
         emit layoutChanged();
      });
      reset->setEnabled(pimpl->layout.tableColors.contains(name));
   }
   else
   {
      menu.addAction(tr("Auto-arrange"), this, [this]() {emit autoArrangeRequested();});
      menu.addAction(tr("Fit to Window"), this, [this]() {emit fitRequested();});
   }
   menu.exec(event->screenPos());
}
