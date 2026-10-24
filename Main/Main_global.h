#pragma once

#include <QtCore/qglobal.h>

#if defined(MAIN_LIBRARY)
#  define MAIN_EXPORT Q_DECL_EXPORT
#else
#  define MAIN_EXPORT Q_DECL_IMPORT
#endif
