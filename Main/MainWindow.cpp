#include "MainWindow.h"
#include "Theme.h"
#include "Diagram/DiagramScene.h"
#include "Diagram/DiagramView.h"
#include "Editor/DbmlEditor.h"
#include "Editor/FindBar.h"
#include "Export/ImageExporter.h"
#include "Dbml/DbmlParser.h"
#include "Dbml/DbmlWriter.h"
#include "Sql/SqlExporter.h"
#include "Sql/SqlImporter.h"
#include "Layout/LayoutMapper.h"
#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDragEnterEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QSaveFile>
#include <QSettings>
#include <QSplitter>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTextBrowser>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QUndoStack>
#include <QVBoxLayout>

namespace
{
   const int MaxRecentFiles = 10;
   const int ParseDelayMs = 250;

   QString layoutPath(const QString& dbmlPath)
   {
      const QFileInfo info(dbmlPath);
      return info.dir().filePath(info.completeBaseName() + QStringLiteral(".layout.json"));
   }

   QString readTextFile(const QString& path, QString* error)
   {
      QFile file(path);
      if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
      {
         *error = file.errorString();
         return QString();
      }
      return QString::fromUtf8(file.readAll());
   }

   bool writeFile(const QString& path, const QByteArray& data, QString* error)
   {
      QSaveFile file(path);
      if (!file.open(QIODevice::WriteOnly))
      {
         *error = file.errorString();
         return false;
      }
      file.write(data);
      if (!file.commit())
      {
         *error = file.errorString();
         return false;
      }
      return true;
   }

   // "1 table", "2 tables"
   QString count(int n, const char* one, const char* many)
   {
      return QStringLiteral("%1 %2").arg(n).arg(QLatin1String(n == 1 ? one : many));
   }

   QIcon icon(const QString& name)
   {
      return QIcon(QStringLiteral(":/icons/%1.svg").arg(name));
   }
}

struct MainWindow::Impl
{
   explicit Impl(MainWindow* window) : window(window) {}

   static QString tr(const char* text, const char* disambiguation = nullptr, int n = -1)
   {
      return MainWindow::tr(text, disambiguation, n);
   }

   void createWidgets();
   void createActions();
   void restoreSettings();
   void saveSettings();

   void reparse();
   void loadDocument(const QString& text, const LayoutDTO& layout, const QString& path);
   bool maybeSave();
   bool save();
   bool saveAs();
   bool saveTo(const QString& path);
   void newDocument();
   void open();
   void importSqlDialog();
   void exportSql(SqlExporter::Dialect);
   void exportImage(const QString& format);
   void copyImage();
   void updateTitle();
   void updateStatus();
   void addRecentFile(const QString& path);
   void updateRecentMenu();
   void applyTheme(bool dark);
   void undo();
   void redo();
   void goToLineDialog();
   void showReference();
   QString exportBaseName() const;
   QString dialogDirectory() const;

   MainWindow* window = nullptr;
   DbmlEditor* editor = nullptr;
   FindBar* findBar = nullptr;
   QListWidget* errorList = nullptr;
   QLabel* staleBanner = nullptr;
   DiagramScene* scene = nullptr;
   DiagramView* view = nullptr;
   QSplitter* splitter = nullptr;
   QWidget* editorPanel = nullptr;
   QLabel* countsLabel = nullptr;
   QLabel* parseLabel = nullptr;
   QLabel* zoomLabel = nullptr;
   QTimer* parseTimer = nullptr;
   QMenu* recentMenu = nullptr;

   QAction* showEditorAction = nullptr;
   QAction* darkAction = nullptr;
   QAction* snapAction = nullptr;
   QActionGroup* detailGroup = nullptr;
   QActionGroup* notationGroup = nullptr;

   QString filePath;
   bool layoutDirty = false;
   bool syncingFromDiagram = false;
   bool firstShow = true;
   int lastCursorLine = -1;
   int errorCount = 0;
};

MainWindow::MainWindow(QWidget *parent)
   : QMainWindow(parent)
{
   pimpl = std::make_unique<Impl>(this);
   setAcceptDrops(true);
   setWindowIcon(icon(QStringLiteral("app")));
   pimpl->createWidgets();
   pimpl->createActions();
   pimpl->restoreSettings();
   pimpl->newDocument();
}

MainWindow::~MainWindow() = default;

void MainWindow::Impl::createWidgets()
{
   // left: find bar, editor, errors
   editorPanel = new QWidget(window);
   auto editorLayout = new QVBoxLayout(editorPanel);
   editorLayout->setContentsMargins(0, 0, 0, 0);
   editorLayout->setSpacing(0);
   editor = new DbmlEditor(editorPanel);
   findBar = new FindBar(editor, editorPanel);
   errorList = new QListWidget(editorPanel);
   errorList->setMaximumHeight(110);
   errorList->setFrameShape(QFrame::NoFrame);
   errorList->hide();
   editorLayout->addWidget(findBar);
   editorLayout->addWidget(editor, 1);
   editorLayout->addWidget(errorList);

   // right: the diagram with a banner when it does not match the text
   auto diagramPanel = new QWidget(window);
   auto diagramLayout = new QVBoxLayout(diagramPanel);
   diagramLayout->setContentsMargins(0, 0, 0, 0);
   diagramLayout->setSpacing(0);
   staleBanner = new QLabel(diagramPanel);
   staleBanner->setWordWrap(true);
   staleBanner->setContentsMargins(10, 5, 10, 5);
   staleBanner->setStyleSheet(QStringLiteral("QLabel { background: #fdecea; color: #b71c1c; }"));
   staleBanner->hide();
   scene = new DiagramScene(window);
   view = new DiagramView(diagramPanel);
   view->setScene(scene);
   diagramLayout->addWidget(staleBanner);
   diagramLayout->addWidget(view, 1);

   splitter = new QSplitter(Qt::Horizontal, window);
   splitter->addWidget(editorPanel);
   splitter->addWidget(diagramPanel);
   splitter->setStretchFactor(0, 0);
   splitter->setStretchFactor(1, 1);
   splitter->setSizes({430, 870});
   splitter->setChildrenCollapsible(false);
   window->setCentralWidget(splitter);

   countsLabel = new QLabel(window);
   parseLabel = new QLabel(window);
   zoomLabel = new QLabel(window);
   zoomLabel->setMinimumWidth(50);
   zoomLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
   window->statusBar()->addWidget(parseLabel);
   window->statusBar()->addWidget(countsLabel);
   window->statusBar()->addPermanentWidget(zoomLabel);

   parseTimer = new QTimer(window);
   parseTimer->setSingleShot(true);
   parseTimer->setInterval(ParseDelayMs);

   connect(parseTimer, &QTimer::timeout, window, [this]() {reparse();});
   connect(editor, &QPlainTextEdit::textChanged, window, [this]() {parseTimer->start();});
   connect(editor->document(), &QTextDocument::modificationChanged, window, [this]() {updateTitle();});
   connect(editor, &QPlainTextEdit::cursorPositionChanged, window, [this]() {
      const int line = editor->currentLine();
      if (line == lastCursorLine)
      {
         return;
      }
      lastCursorLine = line;
      if (!syncingFromDiagram)
      {
         scene->selectByLine(line);
      }
   });
   connect(errorList, &QListWidget::itemActivated, window, [this](QListWidgetItem* item) {
      editor->goToLine(item->data(Qt::UserRole).toInt());
      editor->setFocus();
   });
   connect(errorList, &QListWidget::itemClicked, window, [this](QListWidgetItem* item) {
      editor->goToLine(item->data(Qt::UserRole).toInt());
      editor->setFocus();
   });
   connect(scene, &DiagramScene::lineActivated, window, [this](int line) {
      syncingFromDiagram = true;
      editor->goToLine(line);
      lastCursorLine = editor->currentLine();
      syncingFromDiagram = false;
   });
   connect(scene, &DiagramScene::layoutChanged, window, [this]() {
      layoutDirty = true;
      updateTitle();
   });
   connect(scene, &DiagramScene::autoArrangeRequested, window, [this]() {scene->autoArrange();});
   connect(scene, &DiagramScene::fitRequested, view, &DiagramView::fitToContents);
   connect(view, &DiagramView::zoomChanged, window, [this](qreal zoom) {
      zoomLabel->setText(QStringLiteral("%1%").arg(qRound(zoom * 100)));
   });
   zoomLabel->setText(QStringLiteral("100%"));
}

void MainWindow::Impl::createActions()
{
   QToolBar* toolBar = window->addToolBar(tr("Main"));
   toolBar->setObjectName(QStringLiteral("MainToolBar"));
   toolBar->setMovable(false);
   toolBar->setIconSize(QSize(18, 18));
   toolBar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);

   // ---- File ----
   QMenu* file = window->menuBar()->addMenu(tr("&File"));
   QAction* newAction = file->addAction(icon("new"), tr("&New"), window, [this]() {
      if (maybeSave())
      {
         newDocument();
      }
   });
   newAction->setShortcut(QKeySequence::New);
   QAction* openAction = file->addAction(icon("open"), tr("&Open..."), window, [this]() {open();});
   openAction->setShortcut(QKeySequence::Open);
   recentMenu = file->addMenu(tr("Open &Recent"));
   file->addSeparator();
   QAction* saveAction = file->addAction(icon("save"), tr("&Save"), window, [this]() {save();});
   saveAction->setShortcut(QKeySequence::Save);
   QAction* saveAsAction = file->addAction(tr("Save &As..."), window, [this]() {saveAs();});
   saveAsAction->setShortcut(QKeySequence::SaveAs);
   file->addSeparator();

   QMenu* importMenu = file->addMenu(icon("import"), tr("&Import"));
   QAction* importAction = importMenu->addAction(tr("From &SQL File..."), window, [this]() {importSqlDialog();});
   importAction->setShortcut(QKeySequence(Qt::CTRL + Qt::Key_I));

   QMenu* exportMenu = file->addMenu(icon("export"), tr("&Export"));
   exportMenu->addAction(tr("&PostgreSQL..."), window, [this]() {exportSql(SqlExporter::Dialect::PostgreSQL);});
   exportMenu->addAction(tr("&MySQL..."), window, [this]() {exportSql(SqlExporter::Dialect::MySQL);});
   exportMenu->addAction(tr("SQL &Server..."), window, [this]() {exportSql(SqlExporter::Dialect::SQLServer);});
   exportMenu->addSeparator();
   exportMenu->addAction(tr("P&NG Image..."), window, [this]() {exportImage(QStringLiteral("png"));});
   exportMenu->addAction(tr("S&VG Image..."), window, [this]() {exportImage(QStringLiteral("svg"));});
   exportMenu->addAction(tr("P&DF Document..."), window, [this]() {exportImage(QStringLiteral("pdf"));});
   exportMenu->addSeparator();
   QAction* copyImageAction = exportMenu->addAction(tr("&Copy Diagram as Image"), window, [this]() {copyImage();});
   copyImageAction->setShortcut(QKeySequence(Qt::CTRL + Qt::SHIFT + Qt::Key_C));
   file->addSeparator();
   QAction* quitAction = file->addAction(tr("&Quit"), window, &QWidget::close);
   quitAction->setShortcut(QKeySequence::Quit);

   // ---- Edit ----
   QMenu* edit = window->menuBar()->addMenu(tr("&Edit"));
   QAction* undoAction = edit->addAction(icon("undo"), tr("&Undo"), window, [this]() {undo();});
   undoAction->setShortcut(QKeySequence::Undo);
   undoAction->setToolTip(tr("Undo in the editor or on the diagram, whichever has the focus"));
   QAction* redoAction = edit->addAction(icon("redo"), tr("&Redo"), window, [this]() {redo();});
   redoAction->setShortcut(QKeySequence::Redo);
   edit->addSeparator();
   edit->addAction(tr("Cu&t"), editor, &QPlainTextEdit::cut)->setShortcut(QKeySequence::Cut);
   edit->addAction(tr("&Copy"), editor, &QPlainTextEdit::copy)->setShortcut(QKeySequence::Copy);
   edit->addAction(tr("&Paste"), editor, &QPlainTextEdit::paste)->setShortcut(QKeySequence::Paste);
   edit->addAction(tr("Select &All"), editor, &QPlainTextEdit::selectAll)->setShortcut(QKeySequence::SelectAll);
   edit->addSeparator();
   edit->addAction(tr("&Find..."), findBar, &FindBar::showFind)->setShortcut(QKeySequence::Find);
   edit->addAction(tr("&Replace..."), findBar, &FindBar::showReplace)->setShortcut(QKeySequence(Qt::CTRL + Qt::Key_H));
   edit->addAction(tr("Find &Next"), findBar, &FindBar::findNext)->setShortcut(QKeySequence::FindNext);
   edit->addAction(tr("Find Pre&vious"), findBar, &FindBar::findPrevious)->setShortcut(QKeySequence::FindPrevious);
   edit->addSeparator();
   edit->addAction(tr("Toggle &Comment"), editor, &DbmlEditor::toggleComment)->setShortcut(QKeySequence(Qt::CTRL + Qt::Key_Slash));
   edit->addAction(tr("&Go to Line..."), window, [this]() {goToLineDialog();})->setShortcut(QKeySequence(Qt::CTRL + Qt::Key_G));

   // ---- View ----
   QMenu* viewMenu = window->menuBar()->addMenu(tr("&View"));
   showEditorAction = viewMenu->addAction(tr("Show &Editor"));
   showEditorAction->setCheckable(true);
   showEditorAction->setChecked(true);
   showEditorAction->setShortcut(QKeySequence(Qt::CTRL + Qt::Key_E));
   connect(showEditorAction, &QAction::toggled, editorPanel, &QWidget::setVisible);
   viewMenu->addSeparator();

   QMenu* fields = viewMenu->addMenu(tr("Show &Fields"));
   detailGroup = new QActionGroup(window);
   auto addDetail = [&](const QString& text, DiagramStyle::Detail detail, const QKeySequence& shortcut) {
      QAction* action = fields->addAction(text);
      action->setCheckable(true);
      action->setData(int(detail));
      action->setShortcut(shortcut);
      detailGroup->addAction(action);
      connect(action, &QAction::triggered, window, [this, detail]() {scene->setDetail(detail);});
   };
   addDetail(tr("&All Fields"), DiagramStyle::Detail::AllFields, QKeySequence(Qt::ALT + Qt::Key_1));
   addDetail(tr("&Keys and Refs Only"), DiagramStyle::Detail::KeysOnly, QKeySequence(Qt::ALT + Qt::Key_2));
   addDetail(tr("&Table Names Only"), DiagramStyle::Detail::TableNames, QKeySequence(Qt::ALT + Qt::Key_3));

   QMenu* notation = viewMenu->addMenu(tr("Relationship &Notation"));
   notationGroup = new QActionGroup(window);
   auto addNotation = [&](const QString& text, DiagramStyle::Notation value) {
      QAction* action = notation->addAction(text);
      action->setCheckable(true);
      action->setData(int(value));
      notationGroup->addAction(action);
      connect(action, &QAction::triggered, window, [this, value]() {scene->setNotation(value);});
   };
   addNotation(tr("&Labels (1 and *)"), DiagramStyle::Notation::Labels);
   addNotation(tr("&Crow's Foot"), DiagramStyle::Notation::CrowsFoot);

   snapAction = viewMenu->addAction(tr("&Snap to Grid"));
   snapAction->setCheckable(true);
   connect(snapAction, &QAction::toggled, window, [this](bool on) {scene->setSnapToGrid(on);});
   darkAction = viewMenu->addAction(tr("&Dark Mode"));
   darkAction->setCheckable(true);
   connect(darkAction, &QAction::toggled, window, [this](bool on) {applyTheme(on);});
   viewMenu->addSeparator();
   QAction* zoomInAction = viewMenu->addAction(icon("zoom-in"), tr("Zoom &In"), view, &DiagramView::zoomIn);
   zoomInAction->setShortcuts({QKeySequence::ZoomIn, QKeySequence(Qt::CTRL + Qt::Key_Equal)});
   QAction* zoomOutAction = viewMenu->addAction(icon("zoom-out"), tr("Zoom &Out"), view, &DiagramView::zoomOut);
   zoomOutAction->setShortcut(QKeySequence::ZoomOut);
   viewMenu->addAction(tr("&Actual Size"), view, &DiagramView::resetZoom)->setShortcut(QKeySequence(Qt::CTRL + Qt::Key_0));
   QAction* fitAction = viewMenu->addAction(icon("fit"), tr("&Fit to Window"), view, &DiagramView::fitToContents);
   fitAction->setShortcut(QKeySequence(Qt::CTRL + Qt::SHIFT + Qt::Key_F));
   viewMenu->addSeparator();
   viewMenu->addAction(tr("Larger Editor Font"), window, [this]() {editor->zoomText(1);})->setShortcut(QKeySequence(Qt::CTRL + Qt::ALT + Qt::Key_Equal));
   viewMenu->addAction(tr("Smaller Editor Font"), window, [this]() {editor->zoomText(-1);})->setShortcut(QKeySequence(Qt::CTRL + Qt::ALT + Qt::Key_Minus));

   // ---- Diagram ----
   QMenu* diagram = window->menuBar()->addMenu(tr("&Diagram"));
   QAction* arrangeAction = diagram->addAction(icon("arrange"), tr("&Auto-arrange"), scene, &DiagramScene::autoArrange);
   arrangeAction->setShortcut(QKeySequence(Qt::CTRL + Qt::SHIFT + Qt::Key_L));
   arrangeAction->setToolTip(tr("Arrange the tables by their relationships (Ctrl+Shift+L)"));

   // ---- Help ----
   QMenu* help = window->menuBar()->addMenu(tr("&Help"));
   help->addAction(tr("DBML &Reference"), window, [this]() {showReference();})->setShortcut(QKeySequence::HelpContents);
   help->addAction(tr("Load &Example"), window, [this]() {
      if (maybeSave())
      {
         window->loadExample();
      }
   });
   help->addSeparator();
   help->addAction(tr("&About PhysERD"), window, [this]() {
      QMessageBox::about(window, tr("About PhysERD"),
                         tr("<h3>PhysERD %1</h3>"
                            "<p>Database diagrams from DBML: write the schema on the left, see it on the right.</p>"
                            "<p>Positions of the tables are kept in <i>name.layout.json</i> next to <i>name.dbml</i>.</p>"
                            "<p>Built with Qt %2.</p>").arg(QApplication::applicationVersion(), QT_VERSION_STR));
   });

   // ---- tool bar ----
   toolBar->addAction(newAction);
   toolBar->addAction(openAction);
   toolBar->addAction(saveAction);
   toolBar->addSeparator();
   toolBar->addAction(undoAction);
   toolBar->addAction(redoAction);
   toolBar->addSeparator();
   toolBar->addAction(arrangeAction);
   toolBar->addAction(fitAction);
   toolBar->addAction(zoomOutAction);
   toolBar->addAction(zoomInAction);
   toolBar->addSeparator();

   auto fieldsButton = new QToolButton(toolBar);
   fieldsButton->setText(tr("Fields"));
   fieldsButton->setIcon(icon("fields"));
   fieldsButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
   fieldsButton->setMenu(fields);
   fieldsButton->setPopupMode(QToolButton::InstantPopup);
   toolBar->addWidget(fieldsButton);

   auto importButton = new QToolButton(toolBar);
   importButton->setText(tr("Import"));
   importButton->setIcon(icon("import"));
   importButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
   importButton->setMenu(importMenu);
   importButton->setPopupMode(QToolButton::InstantPopup);
   toolBar->addWidget(importButton);

   auto exportButton = new QToolButton(toolBar);
   exportButton->setText(tr("Export"));
   exportButton->setIcon(icon("export"));
   exportButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
   exportButton->setMenu(exportMenu);
   exportButton->setPopupMode(QToolButton::InstantPopup);
   toolBar->addWidget(exportButton);

   // the long labels only for the main actions
   for (QAction* action : {undoAction, redoAction, zoomInAction, zoomOutAction, newAction, openAction, saveAction, fitAction})
   {
      if (QWidget* button = toolBar->widgetForAction(action))
      {
         if (auto toolButton = qobject_cast<QToolButton*>(button))
         {
            toolButton->setToolButtonStyle(Qt::ToolButtonIconOnly);
            toolButton->setToolTip(action->text().remove('&') + (action->shortcut().isEmpty() ? QString() : QStringLiteral(" (%1)").arg(action->shortcut().toString(QKeySequence::NativeText))));
         }
      }
   }
}

void MainWindow::Impl::restoreSettings()
{
   QSettings settings;
   window->restoreGeometry(settings.value(QStringLiteral("window/geometry")).toByteArray());
   window->restoreState(settings.value(QStringLiteral("window/state")).toByteArray());
   if (settings.contains(QStringLiteral("window/splitter")))
   {
      splitter->restoreState(settings.value(QStringLiteral("window/splitter")).toByteArray());
   }
   if (window->geometry().width() < 400)
   {
      window->resize(1300, 820);
   }
   showEditorAction->setChecked(settings.value(QStringLiteral("view/editor"), true).toBool());

   const int detail = settings.value(QStringLiteral("view/detail"), int(DiagramStyle::Detail::AllFields)).toInt();
   for (QAction* action : detailGroup->actions())
   {
      if (action->data().toInt() == detail)
      {
         action->setChecked(true);
         scene->setDetail(DiagramStyle::Detail(detail));
      }
   }
   const int notation = settings.value(QStringLiteral("view/notation"), int(DiagramStyle::Notation::Labels)).toInt();
   for (QAction* action : notationGroup->actions())
   {
      if (action->data().toInt() == notation)
      {
         action->setChecked(true);
         scene->setNotation(DiagramStyle::Notation(notation));
      }
   }
   snapAction->setChecked(settings.value(QStringLiteral("view/snap"), false).toBool());
   const bool dark = settings.value(QStringLiteral("view/dark"), false).toBool();
   darkAction->setChecked(dark);
   applyTheme(dark);

   const qreal fontSize = settings.value(QStringLiteral("editor/fontSize"), 0).toReal();
   if (fontSize > 0)
   {
      QFont font = editor->font();
      font.setPointSizeF(fontSize);
      editor->setEditorFont(font);
   }
   updateRecentMenu();
}

void MainWindow::Impl::saveSettings()
{
   QSettings settings;
   settings.setValue(QStringLiteral("window/geometry"), window->saveGeometry());
   settings.setValue(QStringLiteral("window/state"), window->saveState());
   settings.setValue(QStringLiteral("window/splitter"), splitter->saveState());
   settings.setValue(QStringLiteral("view/editor"), showEditorAction->isChecked());
   settings.setValue(QStringLiteral("view/detail"), int(scene->style().detail));
   settings.setValue(QStringLiteral("view/notation"), int(scene->style().notation));
   settings.setValue(QStringLiteral("view/snap"), snapAction->isChecked());
   settings.setValue(QStringLiteral("view/dark"), darkAction->isChecked());
   settings.setValue(QStringLiteral("editor/fontSize"), editor->font().pointSizeF());
   settings.setValue(QStringLiteral("lastFile"), filePath);
}

void MainWindow::Impl::applyTheme(bool dark)
{
   const Theme theme = dark ? Theme::darkTheme() : Theme::light();
   QApplication::setPalette(theme.palette());
   scene->setTheme(theme);
   editor->setTheme(theme);
   errorList->setStyleSheet(QStringLiteral("QListWidget { background: %1; color: %2; border-top: 1px solid %3; }")
                            .arg(theme.gutterBackground.name(), theme.error.name(), theme.tableBorder.name()));
   staleBanner->setStyleSheet(dark ? QStringLiteral("QLabel { background: #4a1f1f; color: #ffb4ab; }")
                                   : QStringLiteral("QLabel { background: #fdecea; color: #b71c1c; }"));
}

void MainWindow::Impl::reparse()
{
   parseTimer->stop();
   const DbmlParseResult result = DbmlParser::parse(editor->toPlainText());
   editor->setErrors(result.errors);
   errorCount = result.errors.size();

   errorList->clear();
   for (const DbmlError& error : result.errors)
   {
      auto item = new QListWidgetItem(tr("Line %1: %2").arg(error.line).arg(error.message), errorList);
      item->setData(Qt::UserRole, error.line);
   }
   errorList->setVisible(!result.errors.isEmpty());

   if (result.ok() || !scene->hasTables())
   {
      // with errors the last good diagram stays, unless there is none yet
      scene->setSchema(result.schema);
      staleBanner->setVisible(!result.ok());
   }
   else
   {
      staleBanner->setVisible(true);
   }
   if (staleBanner->isVisible())
   {
      staleBanner->setText(result.ok() ? QString()
                                       : tr("The diagram shows the last valid version: fix %1 in the text to update it.").arg(count(errorCount, "error", "errors")));
   }
   updateStatus();
}

void MainWindow::Impl::updateStatus()
{
   const SchemaDTO& schema = scene->schema();
   QStringList parts;
   parts << count(schema.tables.size(), "table", "tables");
   parts << count(schema.refs.size(), "reference", "references");
   if (!schema.enums.isEmpty())
   {
      parts << count(schema.enums.size(), "enum", "enums");
   }
   if (!schema.tableGroups.isEmpty())
   {
      parts << count(schema.tableGroups.size(), "group", "groups");
   }
   countsLabel->setText(parts.join(QStringLiteral(" · ")));
   if (errorCount == 0)
   {
      parseLabel->setText(QStringLiteral("<span style='color:#2e7d32'>✔</span> ") + tr("No errors"));
   }
   else
   {
      parseLabel->setText(QStringLiteral("<span style='color:#e53935'>✖</span> ") + count(errorCount, "error", "errors"));
   }
}

void MainWindow::Impl::loadDocument(const QString &text, const LayoutDTO &layout, const QString &path)
{
   filePath = path;
   scene->setSchema(SchemaDTO());
   scene->setLayout(layout);
   editor->setPlainText(text);
   reparse();
   editor->document()->setModified(false);
   editor->document()->clearUndoRedoStacks();
   scene->undoStack()->clear();
   layoutDirty = false;
   lastCursorLine = -1;
   editor->moveCursor(QTextCursor::Start);
   if (layout.isEmpty() && !text.trimmed().isEmpty())
   {
      // no saved positions: the arrangement is part of the document now
      layoutDirty = !path.isEmpty();
   }
   updateTitle();
   if (window->isVisible())
   {
      QTimer::singleShot(0, view, &DiagramView::fitToContents);
   }
}

void MainWindow::Impl::newDocument()
{
   loadDocument(QStringLiteral(
                   "// Use DBML to define your database structure\n"
                   "// Docs: Help > DBML Reference (F1)\n\n"
                   "Table users {\n"
                   "  id integer [pk, increment]\n"
                   "  username varchar [not null, unique]\n"
                   "  created_at timestamp [default: `now()`]\n"
                   "}\n"),
                LayoutDTO(), QString());
}

void MainWindow::loadExample()
{
   QString error;
   const QString text = readTextFile(QStringLiteral(":/example.dbml"), &error);
   pimpl->loadDocument(text, LayoutDTO(), QString());
}

bool MainWindow::hasFilePath() const
{
   return !pimpl->filePath.isEmpty();
}

bool MainWindow::openFile(const QString &path)
{
   QString error;
   const QString text = readTextFile(path, &error);
   if (!error.isEmpty())
   {
      QMessageBox::warning(this, tr("Open"), tr("Cannot open %1:\n%2").arg(QDir::toNativeSeparators(path), error));
      return false;
   }
   LayoutDTO layout;
   QFile layoutFile(layoutPath(path));
   if (layoutFile.open(QIODevice::ReadOnly))
   {
      layout = LayoutMapper::fromJson(layoutFile.readAll());
   }
   pimpl->loadDocument(text, layout, QFileInfo(path).absoluteFilePath());
   pimpl->layoutDirty = false;
   pimpl->updateTitle();
   pimpl->addRecentFile(pimpl->filePath);
   return true;
}

bool MainWindow::importSql(const QString &path)
{
   QString error;
   const QString sql = readTextFile(path, &error);
   if (!error.isEmpty())
   {
      QMessageBox::warning(this, tr("Import SQL"), tr("Cannot open %1:\n%2").arg(QDir::toNativeSeparators(path), error));
      return false;
   }
   const SqlImportResult result = SqlImporter::import(sql);
   if (result.schema.tables.isEmpty())
   {
      QMessageBox::warning(this, tr("Import SQL"), tr("No CREATE TABLE statements were found in %1.").arg(QFileInfo(path).fileName()));
      return false;
   }
   const QString dbml = QStringLiteral("// Imported from %1\n\n").arg(QFileInfo(path).fileName()) + DbmlWriter::write(result.schema) + '\n';
   pimpl->loadDocument(dbml, LayoutDTO(), QString());
   pimpl->editor->document()->setModified(true);
   pimpl->updateTitle();

   if (!result.warnings.isEmpty())
   {
      QMessageBox box(QMessageBox::Information, tr("Import SQL"),
                      tr("Imported %1. Some statements were skipped.").arg(count(result.schema.tables.size(), "table", "tables")),
                      QMessageBox::Ok, this);
      box.setDetailedText(result.warnings.join('\n'));
      box.exec();
   }
   else
   {
      statusBar()->showMessage(tr("Imported %1").arg(count(result.schema.tables.size(), "table", "tables")), 4000);
   }
   return true;
}

void MainWindow::Impl::open()
{
   if (!maybeSave())
   {
      return;
   }
   const QString path = QFileDialog::getOpenFileName(window, tr("Open DBML"), dialogDirectory(), tr("DBML files (*.dbml);;Text files (*.txt);;All files (*)"));
   if (!path.isEmpty())
   {
      window->openFile(path);
   }
}

void MainWindow::Impl::importSqlDialog()
{
   if (!maybeSave())
   {
      return;
   }
   const QString path = QFileDialog::getOpenFileName(window, tr("Import SQL"), dialogDirectory(), tr("SQL files (*.sql);;All files (*)"));
   if (!path.isEmpty())
   {
      window->importSql(path);
   }
}

bool MainWindow::Impl::maybeSave()
{
   if (!window->isWindowModified())
   {
      return true;
   }
   const auto answer = QMessageBox::warning(window, tr("Unsaved Changes"),
                                            tr("Save the changes to %1?").arg(filePath.isEmpty() ? tr("the untitled diagram") : QFileInfo(filePath).fileName()),
                                            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
   if (answer == QMessageBox::Save)
   {
      return save();
   }
   return answer == QMessageBox::Discard;
}

bool MainWindow::Impl::save()
{
   return filePath.isEmpty() ? saveAs() : saveTo(filePath);
}

bool MainWindow::Impl::saveAs()
{
   QString suggested = filePath.isEmpty() ? QDir(dialogDirectory()).filePath(exportBaseName() + QStringLiteral(".dbml")) : filePath;
   QString path = QFileDialog::getSaveFileName(window, tr("Save DBML"), suggested, tr("DBML files (*.dbml);;All files (*)"));
   if (path.isEmpty())
   {
      return false;
   }
   if (QFileInfo(path).suffix().isEmpty())
   {
      path += QStringLiteral(".dbml");
   }
   return saveTo(path);
}

bool MainWindow::Impl::saveTo(const QString &path)
{
   QString error;
   if (!writeFile(path, editor->toPlainText().toUtf8(), &error))
   {
      QMessageBox::warning(window, tr("Save"), tr("Cannot save %1:\n%2").arg(QDir::toNativeSeparators(path), error));
      return false;
   }
   if (!writeFile(layoutPath(path), LayoutMapper::toJson(scene->layout()), &error))
   {
      QMessageBox::warning(window, tr("Save"), tr("The text is saved, but the table positions are not:\n%1").arg(error));
   }
   filePath = QFileInfo(path).absoluteFilePath();
   editor->document()->setModified(false);
   layoutDirty = false;
   updateTitle();
   addRecentFile(filePath);
   window->statusBar()->showMessage(tr("Saved %1").arg(QFileInfo(path).fileName()), 3000);
   return true;
}

QString MainWindow::Impl::exportBaseName() const
{
   if (!filePath.isEmpty())
   {
      return QFileInfo(filePath).completeBaseName();
   }
   const ProjectDTO& project = scene->schema().project;
   return project.defined && !project.name.isEmpty() ? project.name : QStringLiteral("diagram");
}

QString MainWindow::Impl::dialogDirectory() const
{
   if (!filePath.isEmpty())
   {
      return QFileInfo(filePath).absolutePath();
   }
   const QString last = QSettings().value(QStringLiteral("lastDirectory")).toString();
   return last.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) : last;
}

void MainWindow::Impl::exportSql(SqlExporter::Dialect dialect)
{
   if (errorCount > 0)
   {
      const auto answer = QMessageBox::question(window, tr("Export SQL"),
                                                tr("The text has errors, the export uses the last valid version of the diagram. Continue?"));
      if (answer != QMessageBox::Yes)
      {
         return;
      }
   }
   const QString suffix = dialect == SqlExporter::Dialect::PostgreSQL ? QStringLiteral("postgres")
                        : dialect == SqlExporter::Dialect::MySQL ? QStringLiteral("mysql") : QStringLiteral("sqlserver");
   const QString suggested = QDir(dialogDirectory()).filePath(exportBaseName() + '_' + suffix + QStringLiteral(".sql"));
   const QString path = QFileDialog::getSaveFileName(window, tr("Export %1").arg(SqlExporter::dialectName(dialect)), suggested, tr("SQL files (*.sql);;All files (*)"));
   if (path.isEmpty())
   {
      return;
   }
   QString error;
   if (!writeFile(path, SqlExporter::exportSchema(scene->schema(), dialect).toUtf8(), &error))
   {
      QMessageBox::warning(window, tr("Export SQL"), tr("Cannot save %1:\n%2").arg(QDir::toNativeSeparators(path), error));
      return;
   }
   QSettings().setValue(QStringLiteral("lastDirectory"), QFileInfo(path).absolutePath());
   window->statusBar()->showMessage(tr("Exported %1").arg(QFileInfo(path).fileName()), 3000);
}

void MainWindow::Impl::exportImage(const QString &format)
{
   const QString filters = format == QLatin1String("png") ? tr("PNG images (*.png)")
                         : format == QLatin1String("svg") ? tr("SVG images (*.svg)") : tr("PDF documents (*.pdf)");
   const QString suggested = QDir(dialogDirectory()).filePath(exportBaseName() + '.' + format);
   QString path = QFileDialog::getSaveFileName(window, tr("Export Diagram"), suggested, filters);
   if (path.isEmpty())
   {
      return;
   }
   if (QFileInfo(path).suffix().isEmpty())
   {
      path += '.' + format;
   }
   bool ok = false;
   if (format == QLatin1String("png"))
   {
      ok = ImageExporter::exportPng(scene, path);
   }
   else if (format == QLatin1String("svg"))
   {
      ok = ImageExporter::exportSvg(scene, path, exportBaseName());
   }
   else
   {
      ok = ImageExporter::exportPdf(scene, path, exportBaseName());
   }
   if (!ok)
   {
      QMessageBox::warning(window, tr("Export Diagram"), tr("Cannot write %1").arg(QDir::toNativeSeparators(path)));
      return;
   }
   QSettings().setValue(QStringLiteral("lastDirectory"), QFileInfo(path).absolutePath());
   window->statusBar()->showMessage(tr("Exported %1").arg(QFileInfo(path).fileName()), 3000);
}

void MainWindow::Impl::copyImage()
{
   QApplication::clipboard()->setImage(ImageExporter::renderImage(scene, 2.0));
   window->statusBar()->showMessage(tr("The diagram is copied to the clipboard"), 3000);
}

void MainWindow::Impl::updateTitle()
{
   const QString name = filePath.isEmpty() ? tr("Untitled") : QFileInfo(filePath).fileName();
   window->setWindowTitle(QStringLiteral("%1[*] — PhysERD").arg(name));
   window->setWindowModified(editor->document()->isModified() || layoutDirty);
}

void MainWindow::Impl::addRecentFile(const QString &path)
{
   QSettings settings;
   QStringList files = settings.value(QStringLiteral("recentFiles")).toStringList();
   files.removeAll(path);
   files.prepend(path);
   while (files.size() > MaxRecentFiles)
   {
      files.removeLast();
   }
   settings.setValue(QStringLiteral("recentFiles"), files);
   settings.setValue(QStringLiteral("lastDirectory"), QFileInfo(path).absolutePath());
   updateRecentMenu();
}

void MainWindow::Impl::updateRecentMenu()
{
   recentMenu->clear();
   const QStringList files = QSettings().value(QStringLiteral("recentFiles")).toStringList();
   for (const QString& path : files)
   {
      QAction* action = recentMenu->addAction(QFileInfo(path).fileName(), window, [this, path]() {
         if (maybeSave())
         {
            window->openFile(path);
         }
      });
      action->setToolTip(QDir::toNativeSeparators(path));
      action->setStatusTip(QDir::toNativeSeparators(path));
   }
   if (!files.isEmpty())
   {
      recentMenu->addSeparator();
      recentMenu->addAction(tr("Clear List"), window, [this]() {
         QSettings().remove(QStringLiteral("recentFiles"));
         updateRecentMenu();
      });
   }
   recentMenu->setEnabled(!files.isEmpty());
}

void MainWindow::Impl::undo()
{
   if (view->hasFocus())
   {
      scene->undoStack()->undo();
   }
   else
   {
      editor->undo();
   }
}

void MainWindow::Impl::redo()
{
   if (view->hasFocus())
   {
      scene->undoStack()->redo();
   }
   else
   {
      editor->redo();
   }
}

void MainWindow::Impl::goToLineDialog()
{
   bool ok = false;
   const int line = QInputDialog::getInt(window, tr("Go to Line"), tr("Line (1 - %1):").arg(editor->blockCount()),
                                         editor->currentLine(), 1, editor->blockCount(), 1, &ok);
   if (ok)
   {
      editor->goToLine(line);
      editor->setFocus();
   }
}

void MainWindow::Impl::showReference()
{
   auto dialog = new QDialog(window);
   dialog->setAttribute(Qt::WA_DeleteOnClose);
   dialog->setWindowTitle(tr("DBML Reference"));
   dialog->resize(720, 760);
   auto layout = new QVBoxLayout(dialog);
   auto browser = new QTextBrowser(dialog);
   QString error;
   browser->setHtml(readTextFile(QStringLiteral(":/reference.html"), &error));
   browser->setOpenExternalLinks(true);
   auto buttons = new QDialogButtonBox(QDialogButtonBox::Close, dialog);
   connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::close);
   layout->addWidget(browser);
   layout->addWidget(buttons);
   dialog->show();
}

void MainWindow::closeEvent(QCloseEvent *event)
{
   if (!pimpl->maybeSave())
   {
      event->ignore();
      return;
   }
   pimpl->saveSettings();
   event->accept();
}

void MainWindow::showEvent(QShowEvent *event)
{
   QMainWindow::showEvent(event);
   if (pimpl->firstShow)
   {
      pimpl->firstShow = false;
      QTimer::singleShot(0, pimpl->view, &DiagramView::fitToContents);
   }
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
   if (event->mimeData()->hasUrls() && event->mimeData()->urls().size() == 1 && event->mimeData()->urls().first().isLocalFile())
   {
      event->acceptProposedAction();
   }
}

void MainWindow::dropEvent(QDropEvent *event)
{
   const QString path = event->mimeData()->urls().first().toLocalFile();
   if (!pimpl->maybeSave())
   {
      return;
   }
   if (QFileInfo(path).suffix().compare(QLatin1String("sql"), Qt::CaseInsensitive) == 0)
   {
      importSql(path);
   }
   else
   {
      openFile(path);
   }
   event->acceptProposedAction();
}
