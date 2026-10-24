#pragma once
#include "Core_global.h"
#include <QByteArray>
#include "LayoutDTO.h"

namespace LayoutMapper
{
   CORE_EXPORT QByteArray toJson(const LayoutDTO&);
   // An empty layout if the data is not a layout
   CORE_EXPORT LayoutDTO fromJson(const QByteArray&, bool* ok = nullptr);
}
