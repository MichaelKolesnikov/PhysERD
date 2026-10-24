INCLUDEPATH += $$PWD
DEPENDPATH  += $$PWD

isEmpty(MAIN_SELF_BUILD) {
    LIBS += -L$$PHYSERD_BIN_DIR -lMain
}

include($$PWD/../PIMPL/PIMPL.pri)
include($$PWD/../Core/Core.pri)
