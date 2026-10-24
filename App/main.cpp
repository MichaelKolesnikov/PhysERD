#include <QApplication>
#include <QCommandLineParser>
#include <QFileInfo>
#include <QLibraryInfo>
#include <QPluginLoader>
#include <QSettings>
#include <QStyleFactory>
#include "MainWindow.h"

bool isXcbPlatformAvailable()
{
   const QString pluginsPath = QLibraryInfo::location(QLibraryInfo::PluginsPath);
   QPluginLoader loader(pluginsPath + "/platforms/libqxcb.so");
   return loader.load();
}

int main(int argc, char **argv)
{
#ifdef Q_OS_LINUX
   if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM") && isXcbPlatformAvailable())
   {
      qputenv("QT_QPA_PLATFORM", "xcb");
   }
#endif
   QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
   QApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);

   QApplication app(argc, argv);
   QApplication::setOrganizationName("PhysERD");
   QApplication::setApplicationName("PhysERD");
   QApplication::setApplicationVersion("1.0.0");
   QApplication::setStyle(QStyleFactory::create("Fusion"));

   QCommandLineParser parser;
   parser.setApplicationDescription("Database diagrams from DBML");
   parser.addHelpOption();
   parser.addVersionOption();
   parser.addPositionalArgument("file", "A .dbml file to open or a .sql file to import");
   parser.process(app);

   auto mainWindow = new MainWindow();
   mainWindow->setAttribute(Qt::WA_DeleteOnClose);

   const QStringList arguments = parser.positionalArguments();
   if (!arguments.isEmpty())
   {
      const QString path = arguments.first();
      if (QFileInfo(path).suffix().compare("sql", Qt::CaseInsensitive) == 0)
      {
         mainWindow->importSql(path);
      }
      else
      {
         mainWindow->openFile(path);
      }
   }
   else
   {
      const QString lastFile = QSettings().value("lastFile").toString();
      if (!lastFile.isEmpty() && QFileInfo::exists(lastFile))
      {
         mainWindow->openFile(lastFile);
      }
      else if (lastFile.isEmpty() && !QSettings().contains("window/geometry"))
      {
         // the very first start shows what DBML looks like
         mainWindow->loadExample();
      }
   }

   mainWindow->show();
   return app.exec();
}
