#include "LayoutMapper.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <cmath>

namespace
{
   const int FormatVersion = 1;

   QJsonObject pointsToJson(const QMap<QString, QPointF>& points)
   {
      QJsonObject object;
      for (auto it = points.cbegin(); it != points.cend(); ++it)
      {
         // whole pixels are enough and keep the file readable
         object.insert(it.key(), QJsonObject{{"x", std::round(it.value().x())}, {"y", std::round(it.value().y())}});
      }
      return object;
   }

   QMap<QString, QPointF> pointsFromJson(const QJsonObject& object)
   {
      QMap<QString, QPointF> points;
      for (auto it = object.constBegin(); it != object.constEnd(); ++it)
      {
         const QJsonObject point = it.value().toObject();
         if (point.contains("x") && point.contains("y"))
         {
            points.insert(it.key(), QPointF(point.value("x").toDouble(), point.value("y").toDouble()));
         }
      }
      return points;
   }
}

QByteArray LayoutMapper::toJson(const LayoutDTO &layout)
{
   QJsonObject root;
   root.insert("version", FormatVersion);
   root.insert("tables", pointsToJson(layout.tables));
   root.insert("notes", pointsToJson(layout.notes));
   if (!layout.tableColors.isEmpty())
   {
      QJsonObject colors;
      for (auto it = layout.tableColors.cbegin(); it != layout.tableColors.cend(); ++it)
      {
         colors.insert(it.key(), it.value());
      }
      root.insert("tableColors", colors);
   }
   return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

LayoutDTO LayoutMapper::fromJson(const QByteArray &data, bool *ok)
{
   QJsonParseError error;
   const QJsonDocument document = QJsonDocument::fromJson(data, &error);
   const bool valid = error.error == QJsonParseError::NoError && document.isObject() && document.object().contains("tables");
   if (ok)
   {
      *ok = valid;
   }
   LayoutDTO layout;
   if (!valid)
   {
      return layout;
   }
   const QJsonObject root = document.object();
   layout.tables = pointsFromJson(root.value("tables").toObject());
   layout.notes = pointsFromJson(root.value("notes").toObject());
   const QJsonObject colors = root.value("tableColors").toObject();
   for (auto it = colors.constBegin(); it != colors.constEnd(); ++it)
   {
      layout.tableColors.insert(it.key(), it.value().toString());
   }
   return layout;
}
