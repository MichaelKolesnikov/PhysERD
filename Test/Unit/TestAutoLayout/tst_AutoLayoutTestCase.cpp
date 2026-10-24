#include <QtTest>
#include <QRectF>
#include "Dbml/DbmlParser.h"
#include "Layout/AutoLayout.h"
#include "Layout/LayoutMapper.h"

class AutoLayoutTestCase : public QObject
{
   Q_OBJECT

private slots:
   void testReferencedTableIsToTheLeft();
   void testNoOverlaps();
   void testCycleTerminates();
   void testGroupsKeepTheirTablesTogether();
   void testLayoutJson();
};

namespace
{
   QMap<QString, QSizeF> sizes(const SchemaDTO& schema, QSizeF size = QSizeF(200, 150))
   {
      QMap<QString, QSizeF> result;
      for (const TableDTO& table : schema.tables)
      {
         result.insert(table.fullName(), size);
      }
      return result;
   }
}

void AutoLayoutTestCase::testReferencedTableIsToTheLeft()
{
   const auto parsed = DbmlParser::parse(
      "Table users { id int }\n"
      "Table posts {\n  id int\n  user_id int [ref: > users.id]\n}\n"
      "Table comments {\n  post_id int [ref: > posts.id]\n}\n"
      "Table lonely { id int }\n");
   QVERIFY(parsed.ok());
   const auto positions = AutoLayout::arrange(parsed.schema, sizes(parsed.schema));
   QCOMPARE(positions.size(), 4);
   QVERIFY(positions["public.users"].x() < positions["public.posts"].x());
   QVERIFY(positions["public.posts"].x() < positions["public.comments"].x());
   // unrelated tables go below the related ones
   QVERIFY(positions["public.lonely"].y() > positions["public.users"].y() + 150);
}

void AutoLayoutTestCase::testNoOverlaps()
{
   QString source = "Table hub { id int }\n";
   for (int i = 0; i < 12; ++i)
   {
      source += QString("Table t%1 {\n  hub_id int [ref: > hub.id]\n}\n").arg(i);
   }
   for (int i = 0; i < 7; ++i)
   {
      source += QString("Table free%1 { id int }\n").arg(i);
   }
   const auto parsed = DbmlParser::parse(source);
   QVERIFY(parsed.ok());
   const QSizeF size(200, 150);
   const auto positions = AutoLayout::arrange(parsed.schema, sizes(parsed.schema, size));
   QCOMPARE(positions.size(), 20);
   QVector<QRectF> rects;
   for (const QPointF& point : positions)
   {
      rects << QRectF(point, size);
   }
   for (int i = 0; i < rects.size(); ++i)
   {
      for (int j = i + 1; j < rects.size(); ++j)
      {
         QVERIFY2(!rects[i].intersects(rects[j]), qPrintable(QString("%1 and %2 overlap").arg(i).arg(j)));
      }
   }
}

void AutoLayoutTestCase::testCycleTerminates()
{
   const auto parsed = DbmlParser::parse(
      "Table a {\n  id int\n  b_id int [ref: > b.id]\n}\n"
      "Table b {\n  id int\n  c_id int [ref: > c.id]\n}\n"
      "Table c {\n  id int\n  a_id int [ref: > a.id]\n  self int [ref: > c.id]\n}\n");
   QVERIFY(parsed.ok());
   const auto positions = AutoLayout::arrange(parsed.schema, sizes(parsed.schema));
   QCOMPARE(positions.size(), 3);
}

void AutoLayoutTestCase::testGroupsKeepTheirTablesTogether()
{
   const auto parsed = DbmlParser::parse(
      "Table users { id int }\n"
      "Table merchants {\n  id int\n  admin int [ref: > users.id]\n}\n"
      "Table products {\n  id int\n  merchant int [ref: > merchants.id]\n}\n"
      "Table orders {\n  user int [ref: > users.id]\n  product int [ref: > products.id]\n}\n"
      "Table audit { id int }\n"
      "TableGroup catalog {\n  merchants\n  products\n}\n");
   QVERIFY(parsed.ok());
   const QSizeF size(200, 150);
   const auto positions = AutoLayout::arrange(parsed.schema, sizes(parsed.schema, size));
   QCOMPARE(positions.size(), 5);

   QRectF frame = QRectF(positions["public.merchants"], size).united(QRectF(positions["public.products"], size));
   frame.adjust(-20, -50, 20, 20); // what the diagram draws around a group
   for (const char* outside : {"public.users", "public.orders", "public.audit"})
   {
      QVERIFY2(!frame.intersects(QRectF(positions[outside], size)), outside);
   }
   QVERIFY(positions["public.users"].x() < positions["public.merchants"].x());
   QVERIFY(positions["public.merchants"].x() < positions["public.products"].x());
}

void AutoLayoutTestCase::testLayoutJson()
{
   LayoutDTO layout;
   layout.tables.insert("public.users", QPointF(10.4, -20));
   layout.notes.insert("todo", QPointF(5, 6));
   layout.tableColors.insert("public.users", "#ff0000");
   bool ok = false;
   const LayoutDTO restored = LayoutMapper::fromJson(LayoutMapper::toJson(layout), &ok);
   QVERIFY(ok);
   QCOMPARE(restored.tables.value("public.users"), QPointF(10, -20));
   QCOMPARE(restored.notes.value("todo"), QPointF(5, 6));
   QCOMPARE(restored.tableColors.value("public.users"), QString("#ff0000"));

   LayoutMapper::fromJson("not json", &ok);
   QVERIFY(!ok);
}

QTEST_APPLESS_MAIN(AutoLayoutTestCase)

#include "tst_AutoLayoutTestCase.moc"
