#include "DbmlWriter.h"
#include "DTO/SchemaDTO.h"
#include <QRegularExpression>
#include <QStringList>

namespace
{
   QString qualified(const QString& schema, const QString& name)
   {
      return schema.isEmpty() ? DbmlWriter::name(name) : DbmlWriter::name(schema) + '.' + DbmlWriter::name(name);
   }

   // integer, varchar(255), "timestamp with time zone", "character varying"(255)
   QString type(const QString& type)
   {
      const int argumentsStart = type.indexOf('(');
      const QString base = argumentsStart < 0 ? type : type.left(argumentsStart);
      const QString arguments = argumentsStart < 0 ? QString() : type.mid(argumentsStart);
      static const QRegularExpression plain(QStringLiteral("^[A-Za-z_][A-Za-z0-9_]*(\\.[A-Za-z_][A-Za-z0-9_]*)?(\\[\\])*$"));
      if (plain.match(base).hasMatch())
      {
         return base + arguments;
      }
      return '"' + QString(base).replace('"', QStringLiteral("\\\"")) + '"' + arguments;
   }

   QString defaultValue(const ColumnDTO& column)
   {
      switch (column.defaultKind)
      {
      case ColumnDTO::StringDefault:
         return DbmlWriter::string(column.defaultValue);
      case ColumnDTO::ExpressionDefault:
         return '`' + column.defaultValue + '`';
      default:
         return column.defaultValue;
      }
   }

   QString settings(const QStringList& items)
   {
      return items.isEmpty() ? QString() : QStringLiteral(" [") + items.join(QStringLiteral(", ")) + ']';
   }

   QString endpoint(const RefEndpointDTO& endpoint)
   {
      QString text = qualified(endpoint.schema, endpoint.table) + '.';
      if (endpoint.columns.size() == 1)
      {
         return text + DbmlWriter::name(endpoint.columns.first());
      }
      QStringList columns;
      for (const QString& column : endpoint.columns)
      {
         columns << DbmlWriter::name(column);
      }
      return text + '(' + columns.join(QStringLiteral(", ")) + ')';
   }

   void writeTable(QString& out, const TableDTO& table)
   {
      out += QStringLiteral("Table ") + qualified(table.schema, table.name);
      if (!table.alias.isEmpty())
      {
         out += QStringLiteral(" as ") + DbmlWriter::name(table.alias);
      }
      if (!table.headerColor.isEmpty())
      {
         out += settings({QStringLiteral("headercolor: ") + table.headerColor});
      }
      out += QStringLiteral(" {\n");

      for (const ColumnDTO& column : table.columns)
      {
         QStringList items;
         if (column.pk)
         {
            items << QStringLiteral("pk");
         }
         if (column.increment)
         {
            items << QStringLiteral("increment");
         }
         if (column.notNull)
         {
            items << QStringLiteral("not null");
         }
         if (column.unique)
         {
            items << QStringLiteral("unique");
         }
         if (column.defaultKind != ColumnDTO::NoDefault)
         {
            items << QStringLiteral("default: ") + defaultValue(column);
         }
         if (!column.note.isEmpty())
         {
            items << QStringLiteral("note: ") + DbmlWriter::string(column.note);
         }
         out += QStringLiteral("  ") + DbmlWriter::name(column.name) + ' ' + type(column.type) + settings(items) + '\n';
      }

      if (!table.indexes.isEmpty())
      {
         out += QStringLiteral("\n  indexes {\n");
         for (const IndexDTO& index : table.indexes)
         {
            QStringList columns;
            for (const IndexColumnDTO& column : index.columns)
            {
               columns << (column.isExpression ? '`' + column.value + '`' : DbmlWriter::name(column.value));
            }
            QStringList items;
            if (index.pk)
            {
               items << QStringLiteral("pk");
            }
            if (index.unique)
            {
               items << QStringLiteral("unique");
            }
            if (!index.name.isEmpty())
            {
               items << QStringLiteral("name: ") + DbmlWriter::string(index.name);
            }
            if (!index.type.isEmpty())
            {
               items << QStringLiteral("type: ") + index.type;
            }
            if (!index.note.isEmpty())
            {
               items << QStringLiteral("note: ") + DbmlWriter::string(index.note);
            }
            const QString definition = columns.size() == 1 ? columns.first() : '(' + columns.join(QStringLiteral(", ")) + ')';
            out += QStringLiteral("    ") + definition + settings(items) + '\n';
         }
         out += QStringLiteral("  }\n");
      }

      if (!table.note.isEmpty())
      {
         out += QStringLiteral("\n  Note: ") + DbmlWriter::string(table.note) + '\n';
      }
      out += QStringLiteral("}\n\n");
   }
}

QString DbmlWriter::name(const QString &name)
{
   static const QRegularExpression plain(QStringLiteral("^[A-Za-z_][A-Za-z0-9_]*$"));
   if (plain.match(name).hasMatch())
   {
      return name;
   }
   return '"' + QString(name).replace('"', QStringLiteral("\\\"")) + '"';
}

QString DbmlWriter::string(const QString &text)
{
   QString escaped = text;
   escaped.replace('\\', QStringLiteral("\\\\"));
   escaped.replace('\'', QStringLiteral("\\'"));
   if (text.contains('\n'))
   {
      return QStringLiteral("'''\n") + escaped + QStringLiteral("\n'''");
   }
   return '\'' + escaped + '\'';
}

QString DbmlWriter::write(const SchemaDTO &schema)
{
   QString out;
   const ProjectDTO& project = schema.project;
   if (project.defined)
   {
      out += QStringLiteral("Project");
      if (!project.name.isEmpty())
      {
         out += ' ' + name(project.name);
      }
      out += QStringLiteral(" {\n");
      if (!project.databaseType.isEmpty())
      {
         out += QStringLiteral("  database_type: ") + string(project.databaseType) + '\n';
      }
      for (auto it = project.settings.cbegin(); it != project.settings.cend(); ++it)
      {
         out += QStringLiteral("  ") + it.key() + QStringLiteral(": ") + string(it.value()) + '\n';
      }
      if (!project.note.isEmpty())
      {
         out += QStringLiteral("  Note: ") + string(project.note) + '\n';
      }
      out += QStringLiteral("}\n\n");
   }

   for (const EnumDTO& enumDTO : schema.enums)
   {
      out += QStringLiteral("Enum ") + qualified(enumDTO.schema, enumDTO.name) + QStringLiteral(" {\n");
      for (const EnumValueDTO& value : enumDTO.values)
      {
         out += QStringLiteral("  ") + name(value.name);
         if (!value.note.isEmpty())
         {
            out += settings({QStringLiteral("note: ") + string(value.note)});
         }
         out += '\n';
      }
      out += QStringLiteral("}\n\n");
   }

   for (const TableDTO& table : schema.tables)
   {
      writeTable(out, table);
   }

   for (const RefDTO& ref : schema.refs)
   {
      QStringList items;
      if (!ref.onDelete.isEmpty())
      {
         items << QStringLiteral("delete: ") + ref.onDelete;
      }
      if (!ref.onUpdate.isEmpty())
      {
         items << QStringLiteral("update: ") + ref.onUpdate;
      }
      if (!ref.color.isEmpty())
      {
         items << QStringLiteral("color: ") + ref.color;
      }
      out += QStringLiteral("Ref");
      if (!ref.name.isEmpty())
      {
         out += ' ' + name(ref.name);
      }
      out += QStringLiteral(": ") + endpoint(ref.left) + ' ' + ref.relationSymbol() + ' ' + endpoint(ref.right) + settings(items) + '\n';
   }
   if (!schema.refs.isEmpty())
   {
      out += '\n';
   }

   for (const TableGroupDTO& group : schema.tableGroups)
   {
      QStringList items;
      if (!group.color.isEmpty())
      {
         items << QStringLiteral("color: ") + group.color;
      }
      out += QStringLiteral("TableGroup ") + name(group.name) + settings(items) + QStringLiteral(" {\n");
      for (const QString& fullName : group.tables)
      {
         const int dot = fullName.indexOf('.');
         const QString tableSchema = fullName.left(dot);
         out += QStringLiteral("  ") + qualified(tableSchema == QLatin1String("public") ? QString() : tableSchema, fullName.mid(dot + 1)) + '\n';
      }
      if (!group.note.isEmpty())
      {
         out += QStringLiteral("  Note: ") + string(group.note) + '\n';
      }
      out += QStringLiteral("}\n\n");
   }

   for (const NoteDTO& note : schema.notes)
   {
      out += QStringLiteral("Note ") + name(note.name) + QStringLiteral(" {\n  ") + string(note.content) + QStringLiteral("\n}\n\n");
   }

   while (out.endsWith(QLatin1String("\n\n")))
   {
      out.chop(1);
   }
   return out;
}
