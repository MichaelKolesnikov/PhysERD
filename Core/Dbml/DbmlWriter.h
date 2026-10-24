#pragma once
#include "Core_global.h"
#include <QString>
struct SchemaDTO;

namespace DbmlWriter
{
   CORE_EXPORT QString write(const SchemaDTO&);

   // name as is, or "quoted" if it is not a plain identifier
   CORE_EXPORT QString name(const QString&);
   // 'text' with escaping, or '''text''' for several lines
   CORE_EXPORT QString string(const QString&);
}
