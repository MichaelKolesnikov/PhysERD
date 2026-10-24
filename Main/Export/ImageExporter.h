#pragma once
#include "Main_global.h"
#include <QImage>
#include <QString>
class DiagramScene;

// Renders the whole diagram without the grid, the selection and the hover marks
namespace ImageExporter
{
   MAIN_EXPORT QImage renderImage(DiagramScene*, qreal scale = 2.0);
   // scale 2 gives a sharp image on high density screens
   MAIN_EXPORT bool exportPng(DiagramScene*, const QString& path, qreal scale = 2.0);
   MAIN_EXPORT bool exportSvg(DiagramScene*, const QString& path, const QString& title);
   MAIN_EXPORT bool exportPdf(DiagramScene*, const QString& path, const QString& title);
}
