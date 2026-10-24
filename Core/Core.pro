QT -= gui

TEMPLATE = lib
TARGET = Core
DEFINES += CORE_LIBRARY
CONFIG += shared

CORE_SELF_BUILD = 1
include($$PWD/Core.pri)

SOURCES += \
    DTO/SchemaDTO.cpp \
    \
    Dbml/DbmlLexer.cpp \
    Dbml/DbmlParser.cpp \
    Dbml/DbmlWriter.cpp \
    \
    Sql/SqlExporter.cpp \
    Sql/SqlImporter.cpp \
    \
    Layout/AutoLayout.cpp \
    Layout/LayoutMapper.cpp

HEADERS += \
    Core_global.h \
    \
    DTO/ColumnDTO.h \
    DTO/EnumDTO.h \
    DTO/IndexDTO.h \
    DTO/NoteDTO.h \
    DTO/ProjectDTO.h \
    DTO/RefDTO.h \
    DTO/SchemaDTO.h \
    DTO/TableDTO.h \
    DTO/TableGroupDTO.h \
    \
    Dbml/DbmlError.h \
    Dbml/DbmlLexer.h \
    Dbml/DbmlParser.h \
    Dbml/DbmlWriter.h \
    \
    Sql/SqlExporter.h \
    Sql/SqlImporter.h \
    \
    Layout/AutoLayout.h \
    Layout/LayoutDTO.h \
    Layout/LayoutMapper.h
