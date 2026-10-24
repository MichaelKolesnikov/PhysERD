#include <QtTest>
#include "Dbml/DbmlParser.h"
#include "Sql/SqlExporter.h"

using Dialect = SqlExporter::Dialect;

namespace
{
   const char* Source =
      "Enum status {\n  active\n  banned\n}\n"
      "Table users {\n"
      "  id integer [pk, increment]\n"
      "  email varchar(255) [unique, not null, note: 'Login']\n"
      "  state status [default: 'active']\n"
      "  verified bool [default: false]\n"
      "  created_at timestamp [default: `now()`]\n"
      "  Note: 'People'\n"
      "}\n"
      "Table posts {\n"
      "  id bigint [pk, increment]\n"
      "  user_id integer\n"
      "  indexes {\n"
      "    user_id [name: 'posts_user']\n"
      "    (id, user_id) [unique]\n"
      "  }\n"
      "}\n"
      "Table tags {\n"
      "  a int\n"
      "  b int\n"
      "  indexes {\n    (a, b) [pk]\n  }\n"
      "}\n"
      "Ref: posts.user_id > users.id [delete: cascade]\n"
      "Ref: users.id < tags.a\n"
      "Ref: posts.id <> tags.b\n";

   SchemaDTO schema()
   {
      const auto result = DbmlParser::parse(Source);
      if (!result.ok())
      {
         qWarning() << result.errors.first().message;
      }
      return result.schema;
   }
}

class SqlExporterTestCase : public QObject
{
   Q_OBJECT

private slots:
   void testPostgreSQL();
   void testMySQL();
   void testSQLServer();
   void testSchemas();
   void testDialectFromDatabaseType();
};

void SqlExporterTestCase::testPostgreSQL()
{
   const QString sql = SqlExporter::exportSchema(schema(), Dialect::PostgreSQL);
   QVERIFY(sql.contains("CREATE TYPE \"status\" AS ENUM (\n  'active',\n  'banned'\n);"));
   QVERIFY(sql.contains("  \"id\" SERIAL PRIMARY KEY,"));
   QVERIFY(sql.contains("  \"email\" varchar(255) UNIQUE NOT NULL,"));
   QVERIFY(sql.contains("  \"state\" \"status\" DEFAULT 'active',"));
   QVERIFY(sql.contains("  \"verified\" bool DEFAULT false,"));
   QVERIFY(sql.contains("  \"created_at\" timestamp DEFAULT (now())\n);"));
   QVERIFY(sql.contains("  \"id\" BIGSERIAL PRIMARY KEY,"));
   QVERIFY(sql.contains("  PRIMARY KEY (\"a\", \"b\")"));
   QVERIFY(sql.contains("COMMENT ON TABLE \"users\" IS 'People';"));
   QVERIFY(sql.contains("COMMENT ON COLUMN \"users\".\"email\" IS 'Login';"));
   QVERIFY(sql.contains("CREATE INDEX \"posts_user\" ON \"posts\" (\"user_id\");"));
   QVERIFY(sql.contains("CREATE UNIQUE INDEX ON \"posts\" (\"id\", \"user_id\");"));
   QVERIFY(sql.contains("ALTER TABLE \"posts\" ADD FOREIGN KEY (\"user_id\") REFERENCES \"users\" (\"id\") ON DELETE CASCADE;"));
   // one-to-many: the foreign key is on the "many" side
   QVERIFY(sql.contains("ALTER TABLE \"tags\" ADD FOREIGN KEY (\"a\") REFERENCES \"users\" (\"id\");"));
   // many-to-many: a junction table
   QVERIFY(sql.contains("CREATE TABLE \"posts_tags\" (\n  \"posts_id\" bigint NOT NULL,\n  \"tags_b\" int NOT NULL,\n  PRIMARY KEY (\"posts_id\", \"tags_b\")\n);"));
   QVERIFY(sql.contains("ALTER TABLE \"posts_tags\" ADD FOREIGN KEY (\"posts_id\") REFERENCES \"posts\" (\"id\");"));
   QVERIFY(sql.contains("ALTER TABLE \"posts_tags\" ADD FOREIGN KEY (\"tags_b\") REFERENCES \"tags\" (\"b\");"));
   // tables come before the foreign keys referencing them
   QVERIFY(sql.indexOf("CREATE TABLE \"tags\"") < sql.indexOf("ALTER TABLE \"tags\""));
}

void SqlExporterTestCase::testMySQL()
{
   const QString sql = SqlExporter::exportSchema(schema(), Dialect::MySQL);
   QVERIFY(!sql.contains("CREATE TYPE"));
   QVERIFY(sql.contains("  `id` integer AUTO_INCREMENT PRIMARY KEY,"));
   QVERIFY(sql.contains("  `email` varchar(255) UNIQUE NOT NULL COMMENT 'Login',"));
   QVERIFY(sql.contains("  `state` ENUM ('active', 'banned') DEFAULT 'active',"));
   QVERIFY(sql.contains(") COMMENT='People';"));
   QVERIFY(sql.contains("CREATE UNIQUE INDEX `posts_index_2` ON `posts` (`id`, `user_id`);"));
   QVERIFY(sql.contains("ALTER TABLE `posts` ADD FOREIGN KEY (`user_id`) REFERENCES `users` (`id`) ON DELETE CASCADE;"));
}

void SqlExporterTestCase::testSQLServer()
{
   const QString sql = SqlExporter::exportSchema(schema(), Dialect::SQLServer);
   QVERIFY(sql.contains("  [id] integer IDENTITY(1, 1) PRIMARY KEY,"));
   QVERIFY(sql.contains("  [state] nvarchar(255) DEFAULT 'active' CHECK ([state] IN ('active', 'banned')),"));
   QVERIFY(sql.contains("  [verified] bool DEFAULT 0,"));
   QVERIFY(sql.contains("@name = N'Table_Description',\n@value = 'People'"));
   QVERIFY(sql.contains(";\nGO\n"));
}

void SqlExporterTestCase::testSchemas()
{
   const auto result = DbmlParser::parse(
      "Enum shop.kind { a }\n"
      "Table shop.items { id int [pk]\n kind shop.kind\n Note: 'Its' }\n"
      "Table public.logs { item int [ref: > shop.items.id] }\n");
   QVERIFY2(result.ok(), qPrintable(result.errors.value(0).message));
   const QString sql = SqlExporter::exportSchema(result.schema, Dialect::PostgreSQL);
   QVERIFY(sql.contains("CREATE SCHEMA IF NOT EXISTS \"shop\";"));
   QVERIFY(!sql.contains("CREATE SCHEMA IF NOT EXISTS \"public\""));
   QVERIFY(sql.contains("CREATE TYPE \"shop\".\"kind\" AS ENUM"));
   QVERIFY(sql.contains("  \"kind\" \"shop\".\"kind\"\n"));
   QVERIFY(sql.contains("ALTER TABLE \"public\".\"logs\" ADD FOREIGN KEY (\"item\") REFERENCES \"shop\".\"items\" (\"id\");"));
}

void SqlExporterTestCase::testDialectFromDatabaseType()
{
   QCOMPARE(SqlExporter::dialectFromDatabaseType("PostgreSQL", Dialect::MySQL), Dialect::PostgreSQL);
   QCOMPARE(SqlExporter::dialectFromDatabaseType("mysql", Dialect::PostgreSQL), Dialect::MySQL);
   QCOMPARE(SqlExporter::dialectFromDatabaseType("SQL Server", Dialect::PostgreSQL), Dialect::SQLServer);
   QCOMPARE(SqlExporter::dialectFromDatabaseType("Oracle", Dialect::MySQL), Dialect::MySQL);
}

QTEST_APPLESS_MAIN(SqlExporterTestCase)

#include "tst_SqlExporterTestCase.moc"
