#include <QtTest>
#include "Sql/SqlImporter.h"
#include "Sql/SqlExporter.h"
#include "Dbml/DbmlParser.h"
#include "Dbml/DbmlWriter.h"

class SqlImporterTestCase : public QObject
{
   Q_OBJECT

private slots:
   void testPostgresDump();
   void testMySQLDump();
   void testSQLServer();
   void testOneToOneFromUniqueKey();
   void testRoundTripThroughDbml();
};

void SqlImporterTestCase::testPostgresDump()
{
   const auto result = SqlImporter::import(
      "-- dump\n"
      "SET statement_timeout = 0;\n"
      "CREATE TYPE public.mood AS ENUM ('sad', 'ok', 'happy');\n"
      "CREATE TABLE public.users (\n"
      "    id integer DEFAULT nextval('users_id_seq'::regclass) NOT NULL,\n"
      "    name character varying(255) DEFAULT 'anon'::character varying NOT NULL,\n"
      "    created timestamp with time zone DEFAULT now(),\n"
      "    current_mood public.mood,\n"
      "    score numeric(10,2) DEFAULT -1,\n"
      "    CONSTRAINT users_pkey PRIMARY KEY (id)\n"
      ");\n"
      "CREATE TABLE posts (\n"
      "  id bigserial PRIMARY KEY,\n"
      "  user_id int REFERENCES users(id) ON DELETE CASCADE,\n"
      "  title text\n"
      ");\n"
      "COMMENT ON TABLE public.users IS 'People';\n"
      "COMMENT ON COLUMN public.users.name IS 'Display name';\n"
      "CREATE UNIQUE INDEX posts_title_idx ON public.posts USING btree (title);\n"
      "CREATE INDEX ON posts (lower(title));\n");

   const SchemaDTO& schema = result.schema;
   QCOMPARE(schema.enums.size(), 1);
   QCOMPARE(schema.enums[0].name, QString("mood"));
   QCOMPARE(schema.enums[0].values.size(), 3);
   QCOMPARE(schema.tables.size(), 2);

   const TableDTO& users = schema.tables[0];
   QCOMPARE(users.name, QString("users"));
   QVERIFY(users.schema.isEmpty());
   QCOMPARE(users.note, QString("People"));
   QCOMPARE(users.columns.size(), 5);
   QVERIFY(users.columns[0].pk);
   QVERIFY(users.columns[0].increment);
   QCOMPARE(users.columns[0].defaultKind, ColumnDTO::NoDefault);
   QCOMPARE(users.columns[1].type, QString("character varying(255)"));
   QCOMPARE(users.columns[1].defaultKind, ColumnDTO::StringDefault);
   QCOMPARE(users.columns[1].defaultValue, QString("anon"));
   QVERIFY(users.columns[1].notNull);
   QCOMPARE(users.columns[1].note, QString("Display name"));
   QCOMPARE(users.columns[2].type, QString("timestamp with time zone"));
   QCOMPARE(users.columns[2].defaultKind, ColumnDTO::ExpressionDefault);
   QCOMPARE(users.columns[2].defaultValue, QString("now()"));
   QCOMPARE(users.columns[3].type, QString("public.mood"));
   QCOMPARE(users.columns[4].type, QString("numeric(10,2)"));
   QCOMPARE(users.columns[4].defaultValue, QString("-1"));

   const TableDTO& posts = schema.tables[1];
   QVERIFY(posts.columns[0].pk);
   QVERIFY(posts.columns[0].increment);
   QCOMPARE(posts.columns[0].type, QString("bigint"));
   QCOMPARE(posts.indexes.size(), 2);
   QVERIFY(posts.indexes[0].unique);
   QCOMPARE(posts.indexes[0].name, QString("posts_title_idx"));
   QCOMPARE(posts.indexes[0].type, QString("btree"));
   QVERIFY(posts.indexes[1].columns[0].isExpression);
   QCOMPARE(posts.indexes[1].columns[0].value, QString("lower(title)"));

   QCOMPARE(schema.refs.size(), 1);
   QCOMPARE(schema.refs[0].left.table, QString("posts"));
   QCOMPARE(schema.refs[0].right.table, QString("users"));
   QCOMPARE(schema.refs[0].relationSymbol(), QString(">"));
   QCOMPARE(schema.refs[0].onDelete, QString("cascade"));
   QVERIFY2(result.warnings.isEmpty(), qPrintable(result.warnings.join('\n')));
}

void SqlImporterTestCase::testMySQLDump()
{
   const auto result = SqlImporter::import(
      "CREATE TABLE IF NOT EXISTS `orders` (\n"
      "  `id` int unsigned NOT NULL AUTO_INCREMENT,\n"
      "  `status` enum('new','paid') NOT NULL DEFAULT 'new' COMMENT 'State',\n"
      "  `customer_id` int NOT NULL,\n"
      "  `key` varchar(10),\n"
      "  `updated` datetime DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,\n"
      "  PRIMARY KEY (`id`),\n"
      "  KEY `idx_customer` (`customer_id`),\n"
      "  UNIQUE KEY `uk` (`customer_id`, `status`),\n"
      "  CONSTRAINT `fk_customer` FOREIGN KEY (`customer_id`) REFERENCES `customers` (`id`) ON UPDATE SET NULL\n"
      ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='All orders';\n"
      "CREATE TABLE customers (id INT PRIMARY KEY);\n");
   const SchemaDTO& schema = result.schema;
   QCOMPARE(schema.tables.size(), 2);
   const TableDTO& orders = schema.tables[0];
   QCOMPARE(orders.note, QString("All orders"));
   QCOMPARE(orders.columns.size(), 5);
   QCOMPARE(orders.columns[0].type, QString("int unsigned"));
   QVERIFY(orders.columns[0].increment);
   QVERIFY(orders.columns[0].pk);
   QCOMPARE(orders.columns[1].type, QString("orders_status_enum"));
   QCOMPARE(orders.columns[1].note, QString("State"));
   QCOMPARE(orders.columns[1].defaultValue, QString("new"));
   QCOMPARE(orders.columns[3].name, QString("key"));
   QCOMPARE(orders.columns[4].defaultKind, ColumnDTO::ExpressionDefault);
   QCOMPARE(orders.columns[4].defaultValue, QString("CURRENT_TIMESTAMP"));
   QCOMPARE(orders.indexes.size(), 2);
   QCOMPARE(orders.indexes[0].name, QString("idx_customer"));
   QVERIFY(orders.indexes[1].unique);
   QCOMPARE(schema.enums.size(), 1);
   QCOMPARE(schema.enums[0].values.size(), 2);
   // the referenced table is defined later in the script
   QCOMPARE(schema.refs.size(), 1);
   QCOMPARE(schema.refs[0].name, QString("fk_customer"));
   QCOMPARE(schema.refs[0].onUpdate, QString("set null"));
   QCOMPARE(schema.tables[1].name, QString("customers"));
   QCOMPARE(schema.tables[1].columns[0].type, QString("int"));
}

void SqlImporterTestCase::testSQLServer()
{
   const auto result = SqlImporter::import(
      "CREATE TABLE [dbo].[Products] (\n"
      "  [Id] INT IDENTITY(1,1) NOT NULL,\n"
      "  [Name] NVARCHAR(100) NOT NULL,\n"
      "  CONSTRAINT [PK_Products] PRIMARY KEY CLUSTERED ([Id] ASC)\n"
      ")\nGO\n"
      "CREATE TABLE [sales].[Lines] ([ProductId] INT NOT NULL)\nGO\n"
      "ALTER TABLE [sales].[Lines] WITH CHECK ADD CONSTRAINT [FK_Lines] FOREIGN KEY ([ProductId]) REFERENCES [dbo].[Products] ([Id])\nGO\n");
   QCOMPARE(result.schema.tables.size(), 2);
   QCOMPARE(result.schema.tables[0].name, QString("Products"));
   QVERIFY(result.schema.tables[0].columns[0].increment);
   QVERIFY(result.schema.tables[0].columns[0].pk);
   QCOMPARE(result.schema.tables[0].columns[1].type, QString("nvarchar(100)"));
   QCOMPARE(result.schema.tables[1].schema, QString("sales"));
   QCOMPARE(result.schema.refs.size(), 1);
   QCOMPARE(result.schema.refs[0].left.tableFullName(), QString("sales.Lines"));
}

void SqlImporterTestCase::testOneToOneFromUniqueKey()
{
   const auto result = SqlImporter::import(
      "CREATE TABLE users (id int primary key);\n"
      "CREATE TABLE profiles (user_id int unique references users);\n");
   QCOMPARE(result.schema.refs.size(), 1);
   QCOMPARE(result.schema.refs[0].relationSymbol(), QString("-"));
   QCOMPARE(result.schema.refs[0].right.columns, QStringList{"id"});
}

void SqlImporterTestCase::testRoundTripThroughDbml()
{
   const auto first = DbmlParser::parse(
      "Enum kind {\n  a\n  b\n}\n"
      "Table users {\n  id integer [pk, increment]\n  email varchar(255) [unique, not null, note: 'Login']\n  k kind [default: 'a']\n}\n"
      "Table posts {\n  id integer [pk]\n  user_id integer [ref: > users.id]\n  indexes {\n    user_id [name: 'p_u']\n  }\n}\n");
   QVERIFY2(first.ok(), qPrintable(first.errors.value(0).message));
   const QString sql = SqlExporter::exportSchema(first.schema, SqlExporter::Dialect::PostgreSQL);
   const auto imported = SqlImporter::import(sql);
   QVERIFY2(imported.warnings.isEmpty(), qPrintable(imported.warnings.join('\n')));

   const QString dbml = DbmlWriter::write(imported.schema);
   const auto second = DbmlParser::parse(dbml);
   QVERIFY2(second.ok(), qPrintable(second.errors.value(0).message + "\n" + dbml));
   QCOMPARE(second.schema.tables.size(), 2);
   QCOMPARE(second.schema.enums.size(), 1);
   QCOMPARE(second.schema.refs.size(), 1);
   const TableDTO& users = second.schema.tables[0];
   QVERIFY(users.columns[0].pk && users.columns[0].increment);
   QCOMPARE(users.columns[0].type, QString("integer"));
   QVERIFY(users.columns[1].unique && users.columns[1].notNull);
   QCOMPARE(users.columns[1].note, QString("Login"));
   QCOMPARE(users.columns[2].type, QString("kind"));
   QCOMPARE(second.schema.tables[1].indexes[0].name, QString("p_u"));
}

QTEST_APPLESS_MAIN(SqlImporterTestCase)

#include "tst_SqlImporterTestCase.moc"
