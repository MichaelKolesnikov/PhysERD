#include <QtTest>
#include "Dbml/DbmlParser.h"
#include "Dbml/DbmlLexer.h"
#include "Dbml/DbmlWriter.h"

class DbmlParserTestCase : public QObject
{
   Q_OBJECT

private slots:
   void testTableWithColumnSettings();
   void testSchemaAliasAndTableSettings();
   void testRefForms();
   void testCompositeRef();
   void testInlineRefThroughAlias();
   void testIndexes();
   void testEnumAndTableGroup();
   void testProjectAndStickyNote();
   void testMultiLineString();
   void testTablePartials();
   void testComments();
   void testErrorRecovery();
   void testUnknownTableInRef();
   void testDuplicateTable();
   void testUnclosedTable();
   void testWriterRoundTrip();
};

void DbmlParserTestCase::testTableWithColumnSettings()
{
   const auto result = DbmlParser::parse(
      "Table users {\n"
      "  id integer [pk, increment]\n"
      "  email varchar(255) [not null, unique, note: 'Login']\n"
      "  balance decimal(10, 2) [default: -1.5]\n"
      "  created_at \"timestamp with time zone\" [default: `now()`]\n"
      "  active boolean [default: true, null]\n"
      "  status varchar [default: 'new']\n"
      "  tags text[]\n"
      "}\n");
   QVERIFY2(result.ok(), qPrintable(result.errors.value(0).message));
   QCOMPARE(result.schema.tables.size(), 1);
   const TableDTO& table = result.schema.tables.first();
   QCOMPARE(table.name, QString("users"));
   QCOMPARE(table.fullName(), QString("public.users"));
   QCOMPARE(table.line, 1);
   QCOMPARE(table.endLine, 9);
   QCOMPARE(table.columns.size(), 7);

   const ColumnDTO& id = table.columns[0];
   QVERIFY(id.pk);
   QVERIFY(id.increment);
   QCOMPARE(id.type, QString("integer"));
   QCOMPARE(id.line, 2);

   const ColumnDTO& email = table.columns[1];
   QCOMPARE(email.type, QString("varchar(255)"));
   QVERIFY(email.notNull);
   QVERIFY(email.unique);
   QCOMPARE(email.note, QString("Login"));

   QCOMPARE(table.columns[2].type, QString("decimal(10,2)"));
   QCOMPARE(table.columns[2].defaultKind, ColumnDTO::NumberDefault);
   QCOMPARE(table.columns[2].defaultValue, QString("-1.5"));

   QCOMPARE(table.columns[3].type, QString("timestamp with time zone"));
   QCOMPARE(table.columns[3].defaultKind, ColumnDTO::ExpressionDefault);
   QCOMPARE(table.columns[3].defaultValue, QString("now()"));

   QCOMPARE(table.columns[4].defaultKind, ColumnDTO::BooleanDefault);
   QVERIFY(!table.columns[4].notNull);
   QCOMPARE(table.columns[5].defaultKind, ColumnDTO::StringDefault);
   QCOMPARE(table.columns[5].defaultValue, QString("new"));
   QCOMPARE(table.columns[6].type, QString("text[]"));
}

void DbmlParserTestCase::testSchemaAliasAndTableSettings()
{
   const auto result = DbmlParser::parse(
      "Table ecommerce.\"order items\" as OI [headercolor: #3498DB, note: 'Lines'] {\n"
      "  id int\n"
      "  Note: 'Overrides the setting'\n"
      "}\n");
   QVERIFY2(result.ok(), qPrintable(result.errors.value(0).message));
   const TableDTO& table = result.schema.tables.first();
   QCOMPARE(table.schema, QString("ecommerce"));
   QCOMPARE(table.name, QString("order items"));
   QCOMPARE(table.alias, QString("OI"));
   QCOMPARE(table.headerColor, QString("#3498DB"));
   QCOMPARE(table.note, QString("Overrides the setting"));
   QCOMPARE(table.displayName(), QString("ecommerce.order items"));
}

void DbmlParserTestCase::testRefForms()
{
   const auto result = DbmlParser::parse(
      "Table users { id int }\n"
      "Table posts {\n"
      "  id int\n"
      "  user_id int\n"
      "  editor_id int\n"
      "}\n"
      "Ref: posts.user_id > users.id [delete: cascade, update: no action]\n"
      "Ref named { users.id < posts.editor_id [color: #ff0000] }\n"
      "Ref {\n"
      "  posts.id - users.id\n"
      "  posts.user_id <> users.id\n"
      "}\n");
   QVERIFY2(result.ok(), qPrintable(result.errors.value(0).message));
   const auto& refs = result.schema.refs;
   QCOMPARE(refs.size(), 4);

   QCOMPARE(refs[0].left.table, QString("posts"));
   QCOMPARE(refs[0].left.columns, QStringList{"user_id"});
   QCOMPARE(refs[0].right.table, QString("users"));
   QCOMPARE(refs[0].relationSymbol(), QString(">"));
   QCOMPARE(refs[0].onDelete, QString("cascade"));
   QCOMPARE(refs[0].onUpdate, QString("no action"));
   QCOMPARE(refs[0].line, 7);

   QCOMPARE(refs[1].name, QString("named"));
   QCOMPARE(refs[1].relationSymbol(), QString("<"));
   QCOMPARE(refs[1].color, QString("#ff0000"));

   QCOMPARE(refs[2].relationSymbol(), QString("-"));
   QCOMPARE(refs[3].relationSymbol(), QString("<>"));
   QCOMPARE(refs[3].line, 11);
}

void DbmlParserTestCase::testCompositeRef()
{
   const auto result = DbmlParser::parse(
      "Table merchants {\n  id int\n  country_code int\n}\n"
      "Table merchant_periods {\n  merchant_id int\n  country_code int\n}\n"
      "Ref: merchant_periods.(merchant_id, country_code) > merchants.(id, country_code)\n");
   QVERIFY2(result.ok(), qPrintable(result.errors.value(0).message));
   QCOMPARE(result.schema.refs.size(), 1);
   QCOMPARE(result.schema.refs[0].left.columns, (QStringList{"merchant_id", "country_code"}));
   QCOMPARE(result.schema.refs[0].right.columns, (QStringList{"id", "country_code"}));

   const auto mismatch = DbmlParser::parse(
      "Table a {\n  x int\n  y int\n}\nTable b { x int }\nRef: a.(x, y) > b.x\n");
   QCOMPARE(mismatch.errors.size(), 1);
   QCOMPARE(mismatch.errors[0].line, 6);
}

void DbmlParserTestCase::testInlineRefThroughAlias()
{
   const auto result = DbmlParser::parse(
      "Table core.users as U { id int [pk] }\n"
      "Table posts {\n"
      "  author int [ref: > U.id, not null]\n"
      "  reviewer int [ref: > core.users.id]\n"
      "}\n");
   QVERIFY2(result.ok(), qPrintable(result.errors.value(0).message));
   QCOMPARE(result.schema.refs.size(), 2);
   for (const RefDTO& ref : result.schema.refs)
   {
      QVERIFY(ref.isInline);
      QCOMPARE(ref.left.tableFullName(), QString("public.posts"));
      QCOMPARE(ref.right.tableFullName(), QString("core.users"));
      QCOMPARE(ref.right.table, QString("users"));
   }
   QVERIFY(result.schema.tables[1].columns[0].notNull);
}

void DbmlParserTestCase::testIndexes()
{
   const auto result = DbmlParser::parse(
      "Table bookings {\n"
      "  id int\n"
      "  country varchar\n"
      "  booking_date date\n"
      "  indexes {\n"
      "    (id, country) [pk]\n"
      "    booking_date [name: 'date_idx', type: hash, note: 'By date']\n"
      "    (country, booking_date) [unique]\n"
      "    `lower(country)`\n"
      "    (`id*2`, country)\n"
      "  }\n"
      "}\n");
   QVERIFY2(result.ok(), qPrintable(result.errors.value(0).message));
   const auto& indexes = result.schema.tables[0].indexes;
   QCOMPARE(indexes.size(), 5);
   QVERIFY(indexes[0].pk);
   QCOMPARE(indexes[0].columns.size(), 2);
   QCOMPARE(indexes[1].name, QString("date_idx"));
   QCOMPARE(indexes[1].type, QString("hash"));
   QCOMPARE(indexes[1].note, QString("By date"));
   QVERIFY(indexes[2].unique);
   QVERIFY(indexes[3].columns[0].isExpression);
   QCOMPARE(indexes[3].columns[0].value, QString("lower(country)"));
   QVERIFY(indexes[4].columns[0].isExpression);
   QVERIFY(!indexes[4].columns[1].isExpression);

   const auto bad = DbmlParser::parse("Table t {\n  a int\n  indexes {\n    b\n  }\n}\n");
   QCOMPARE(bad.errors.size(), 1);
   QCOMPARE(bad.errors[0].line, 4);
}

void DbmlParserTestCase::testEnumAndTableGroup()
{
   const auto result = DbmlParser::parse(
      "Enum shop.order_status {\n"
      "  created [note: 'Waiting']\n"
      "  \"in progress\"\n"
      "  done\n"
      "}\n"
      "Table shop.orders { status shop.order_status }\n"
      "Table users as U { id int }\n"
      "TableGroup sales [color: #27ae60] {\n"
      "  shop.orders\n"
      "  U\n"
      "  Note: 'Everything about sales'\n"
      "}\n");
   QVERIFY2(result.ok(), qPrintable(result.errors.value(0).message));
   QCOMPARE(result.schema.enums.size(), 1);
   const EnumDTO& status = result.schema.enums[0];
   QCOMPARE(status.fullName(), QString("shop.order_status"));
   QCOMPARE(status.values.size(), 3);
   QCOMPARE(status.values[0].note, QString("Waiting"));
   QCOMPARE(status.values[1].name, QString("in progress"));
   QVERIFY(result.schema.enumForType("shop.order_status") == &status);
   QVERIFY(result.schema.enumForType("order_status") == nullptr);

   QCOMPARE(result.schema.tableGroups.size(), 1);
   const TableGroupDTO& group = result.schema.tableGroups[0];
   QCOMPARE(group.tables, (QStringList{"shop.orders", "public.users"}));
   QCOMPARE(group.color, QString("#27ae60"));
   QCOMPARE(group.note, QString("Everything about sales"));
}

void DbmlParserTestCase::testProjectAndStickyNote()
{
   const auto result = DbmlParser::parse(
      "Project shop {\n"
      "  database_type: 'PostgreSQL'\n"
      "  version: '2'\n"
      "  Note: 'The shop'\n"
      "}\n"
      "Note todo [headercolor: #f1c40f] {\n"
      "  'Add payments'\n"
      "}\n");
   QVERIFY2(result.ok(), qPrintable(result.errors.value(0).message));
   QVERIFY(result.schema.project.defined);
   QCOMPARE(result.schema.project.name, QString("shop"));
   QCOMPARE(result.schema.project.databaseType, QString("PostgreSQL"));
   QCOMPARE(result.schema.project.settings.value("version"), QString("2"));
   QCOMPARE(result.schema.project.note, QString("The shop"));
   QCOMPARE(result.schema.notes.size(), 1);
   QCOMPARE(result.schema.notes[0].name, QString("todo"));
   QCOMPARE(result.schema.notes[0].content, QString("Add payments"));
   QCOMPARE(result.schema.notes[0].headerColor, QString("#f1c40f"));
}

void DbmlParserTestCase::testMultiLineString()
{
   const auto result = DbmlParser::parse(
      "Table t {\n"
      "  id int\n"
      "  Note: '''\n"
      "    First line\n"
      "      indented\n"
      "    It\\'s quoted\n"
      "  '''\n"
      "}\n");
   QVERIFY2(result.ok(), qPrintable(result.errors.value(0).message));
   QCOMPARE(result.schema.tables[0].note, QString("First line\n  indented\nIt's quoted"));
   QCOMPARE(result.schema.tables[0].endLine, 8);
}

void DbmlParserTestCase::testTablePartials()
{
   const auto result = DbmlParser::parse(
      "TablePartial base [headercolor: #111111] {\n"
      "  id int [pk]\n"
      "  created_at timestamp\n"
      "  owner_id int [ref: > users.id]\n"
      "}\n"
      "TablePartial audit {\n  created_at timestamptz\n  updated_at timestamptz\n}\n"
      "Table users { id int }\n"
      "Table items {\n"
      "  ~base\n"
      "  name varchar\n"
      "  ~audit\n"
      "  id bigint\n"
      "}\n");
   QVERIFY2(result.ok(), qPrintable(result.errors.value(0).message));
   const TableDTO& items = *result.schema.table("public.items");
   QStringList names;
   for (const ColumnDTO& column : items.columns)
   {
      names << column.name + ':' + column.type;
   }
   // own id wins, the later partial's created_at wins
   QCOMPARE(names, (QStringList{"created_at:timestamptz", "owner_id:int", "name:varchar", "updated_at:timestamptz", "id:bigint"}));
   QCOMPARE(items.headerColor, QString("#111111"));
   QCOMPARE(result.schema.refs.size(), 1);
   QCOMPARE(result.schema.refs[0].left.tableFullName(), QString("public.items"));

   const auto missing = DbmlParser::parse("Table t {\n  ~nothing\n}\n");
   QCOMPARE(missing.errors.size(), 1);
   QCOMPARE(missing.errors[0].line, 2);
}

void DbmlParserTestCase::testComments()
{
   const auto result = DbmlParser::parse(
      "// header comment\n"
      "Table t { // trailing\n"
      "  /* block\n"
      "     comment */ id int\n"
      "}\n");
   QVERIFY2(result.ok(), qPrintable(result.errors.value(0).message));
   QCOMPARE(result.schema.tables[0].columns.size(), 1);
   QCOMPARE(result.schema.tables[0].columns[0].line, 4);
}

void DbmlParserTestCase::testErrorRecovery()
{
   const auto result = DbmlParser::parse(
      "Table a {\n"
      "  id int [pk\n"
      "  name varchar [bogus]\n"
      "}\n"
      "Tabel b {\n"
      "  x int\n"
      "}\n"
      "Table c {\n"
      "  id int\n"
      "}\n");
   QVERIFY(!result.ok());
   QVERIFY(result.errors.size() >= 3);
   // the broken elements do not stop the parsing of the others
   QVERIFY(result.schema.table("public.c") != nullptr);
   QVERIFY(result.schema.table("public.a") != nullptr);
   bool unknownSetting = false;
   bool unexpectedKeyword = false;
   for (const DbmlError& error : result.errors)
   {
      unknownSetting = unknownSetting || (error.line == 3 && error.message.contains("bogus"));
      unexpectedKeyword = unexpectedKeyword || (error.line == 5 && error.column == 1);
   }
   QVERIFY(unknownSetting);
   QVERIFY(unexpectedKeyword);
}

void DbmlParserTestCase::testUnknownTableInRef()
{
   const auto result = DbmlParser::parse("Table a { id int }\nRef: a.id > b.id\nRef: a.nope > a.id\n");
   QCOMPARE(result.errors.size(), 2);
   QCOMPARE(result.errors[0].line, 2);
   QVERIFY(result.errors[0].message.contains("'b'"));
   QCOMPARE(result.errors[1].line, 3);
   QVERIFY(result.errors[1].message.contains("nope"));
   QVERIFY(result.schema.refs.isEmpty());
}

void DbmlParserTestCase::testDuplicateTable()
{
   const auto result = DbmlParser::parse("Table a { id int }\nTable public.a { id int }\nTable s.a { id int }\n");
   QCOMPARE(result.errors.size(), 1);
   QCOMPARE(result.errors[0].line, 2);
}

void DbmlParserTestCase::testUnclosedTable()
{
   const auto result = DbmlParser::parse("Table a {\n  id int\n");
   QCOMPARE(result.errors.size(), 1);
   QCOMPARE(result.errors[0].line, 1);
   QCOMPARE(result.schema.tables.size(), 1);
}

void DbmlParserTestCase::testWriterRoundTrip()
{
   const QString source =
      "Project p {\n  database_type: 'MySQL'\n}\n"
      "Enum status {\n  \"a b\" [note: 'x']\n  c\n}\n"
      "Table s.users as U [headercolor: #abcdef] {\n"
      "  id int [pk, increment]\n"
      "  \"full name\" \"character varying\"(10) [not null, default: 'O\\'Neil', note: 'n']\n"
      "  st status [default: `now()`]\n"
      "  indexes {\n    (id, st) [unique, name: 'i', type: btree]\n  }\n"
      "  Note: '''\n  two\n  lines\n  '''\n"
      "}\n"
      "Table posts {\n  uid int\n}\n"
      "Ref fk: posts.uid > s.users.id [delete: set null]\n"
      "TableGroup g {\n  posts\n}\n"
      "Note n {\n  'sticky'\n}\n";
   const auto first = DbmlParser::parse(source);
   QVERIFY2(first.ok(), qPrintable(first.errors.value(0).message));
   const QString written = DbmlWriter::write(first.schema);
   const auto second = DbmlParser::parse(written);
   QVERIFY2(second.ok(), qPrintable(second.errors.value(0).message + "\n" + written));
   QCOMPARE(DbmlWriter::write(second.schema), written);

   const TableDTO& users = *second.schema.table("s.users");
   QCOMPARE(users.columns[1].name, QString("full name"));
   QCOMPARE(users.columns[1].type, QString("character varying(10)"));
   QCOMPARE(users.columns[1].defaultValue, QString("O'Neil"));
   QCOMPARE(users.note, QString("two\nlines"));
   QCOMPARE(second.schema.refs[0].onDelete, QString("set null"));
   QCOMPARE(second.schema.tableGroups[0].tables, QStringList{"public.posts"});
}

QTEST_APPLESS_MAIN(DbmlParserTestCase)

#include "tst_DbmlParserTestCase.moc"
