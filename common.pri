CONFIG += c++17 warn_on
win32-msvc* {
    QMAKE_CXXFLAGS += /std:c++17 /utf-8
    CONFIG -= c++14
}
