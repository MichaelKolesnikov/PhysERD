QT += core gui widgets svg

TEMPLATE = lib
TARGET = Main
DEFINES += MAIN_LIBRARY
CONFIG += shared

MAIN_SELF_BUILD = 1
include($$PWD/Main.pri)

SOURCES += \
    Theme.cpp \
    \
    Diagram/DiagramScene.cpp \
    Diagram/DiagramView.cpp \
    Diagram/RefItem.cpp \
    Diagram/StickyNoteItem.cpp \
    Diagram/TableGroupItem.cpp \
    Diagram/TableItem.cpp \
    \
    Editor/DbmlEditor.cpp \
    Editor/DbmlHighlighter.cpp \
    Editor/FindBar.cpp \
    \
    Export/ImageExporter.cpp \
    \
    MainWindow.cpp

HEADERS += \
    Main_global.h \
    Theme.h \
    \
    Diagram/DiagramScene.h \
    Diagram/DiagramStyle.h \
    Diagram/DiagramView.h \
    Diagram/ItemType.h \
    Diagram/RefItem.h \
    Diagram/StickyNoteItem.h \
    Diagram/TableGroupItem.h \
    Diagram/TableItem.h \
    \
    Editor/DbmlEditor.h \
    Editor/DbmlHighlighter.h \
    Editor/FindBar.h \
    \
    Export/ImageExporter.h \
    \
    MainWindow.h

RESOURCES += \
    Resources/resources.qrc
