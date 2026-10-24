INCLUDEPATH += $$PWD
DEPENDPATH  += $$PWD

isEmpty(CORE_SELF_BUILD) {
    LIBS += -L$$PHYSERD_BIN_DIR -lCore
}

include($$PWD/../common.pri)
