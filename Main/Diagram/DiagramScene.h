#pragma once
#include "Main_global.h"
#include <QGraphicsScene>
#include "PIMPL.h"
#include "DiagramStyle.h"
#include "DTO/SchemaDTO.h"
#include "Layout/LayoutDTO.h"
class QUndoStack;
class TableItem;
class RefItem;

// The diagram of a schema. Rebuilt on every successful parse, the positions of the tables
// survive the rebuild by their full names
class MAIN_EXPORT DiagramScene : public QGraphicsScene
{
   Q_OBJECT
public:
   explicit DiagramScene(QObject* parent = nullptr);
   ~DiagramScene() override;

   void setSchema(const SchemaDTO&);
   const SchemaDTO& schema() const;
   bool hasTables() const;

   // Positions of the existing tables and notes, and the header colors chosen on the diagram
   LayoutDTO layout() const;
   // Replaces the positions; tables without one are placed next to the others
   void setLayout(const LayoutDTO&);
   void autoArrange();

   const DiagramStyle& style() const;
   void setTheme(const Theme&);
   void setDetail(DiagramStyle::Detail);
   void setNotation(DiagramStyle::Notation);
   void setSnapToGrid(bool);
   void setDiagramFont(const QFont&);
   // Hides the grid, hover and selection while rendering to a file
   void setExporting(bool);

   // Moves on the diagram, separate from the text undo
   QUndoStack* undoStack() const;

   // Selects the element defined at the line of the text
   void selectByLine(int line);
   // Bounding rectangle of the tables, groups and notes
   QRectF contentRect() const;
   TableItem* tableItem(const QString& fullName) const;

   // ---- called by the items ----
   void tableMoved(TableItem*);
   void moveFinished();
   void activate(int line);
   void activateEnum(const QString& type);
   void setHoveredColumn(TableItem*, const QString& column);
   void setHoveredRef(RefItem*);

signals:
   // The user moved something or arranged the diagram
   void layoutChanged();
   // An element was clicked: show its definition
   void lineActivated(int line);
   void autoArrangeRequested();
   void fitRequested();

protected:
   void drawBackground(QPainter*, const QRectF&) override;
   void contextMenuEvent(QGraphicsSceneContextMenuEvent*) override;

PIMPL
};
