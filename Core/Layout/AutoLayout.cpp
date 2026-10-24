#include "AutoLayout.h"
#include "DTO/SchemaDTO.h"
#include <QHash>
#include <QRectF>
#include <QSet>
#include <QVector>
#include <algorithm>
#include <climits>
#include <cmath>

namespace
{
   const qreal ColumnGap = 140;
   const qreal RowGap = 60;
   const qreal GridGap = 60;
   // room for the frame of a table group around its tables: the padding and the title
   const qreal GroupPadding = 40;
   const qreal GroupTitle = 40;

   struct Graph
   {
      int size = 0;
      QVector<QVector<int>> out; // parent ("one" side) -> children
      QVector<QVector<int>> neighbours;

      explicit Graph(int size) : size(size), out(size), neighbours(size) {}

      void addEdge(int from, int to)
      {
         if (from < 0 || to < 0 || from == to || out[from].contains(to) || out[to].contains(from))
         {
            return;
         }
         out[from] << to;
         neighbours[from] << to;
         neighbours[to] << from;
      }
   };

   // The "one" side of a ref is the parent
   QPair<int, int> parentAndChild(const RefDTO& ref, int leftNode, int rightNode)
   {
      if (ref.leftCardinality == Cardinality::One && ref.rightCardinality == Cardinality::Many)
      {
         return {leftNode, rightNode};
      }
      return {rightNode, leftNode};
   }

   // Longest path layering of the graph without the edges closing cycles
   QVector<int> layers(const Graph& graph)
   {
      // drop back edges found by a depth-first search
      QVector<QVector<int>> acyclic(graph.size);
      QVector<int> state(graph.size, 0); // 0 new, 1 on stack, 2 done
      for (int root = 0; root < graph.size; ++root)
      {
         if (state[root] != 0)
         {
            continue;
         }
         QVector<QPair<int, int>> stack{{root, 0}};
         state[root] = 1;
         while (!stack.isEmpty())
         {
            const int node = stack.last().first;
            const int edge = stack.last().second;
            if (edge < graph.out[node].size())
            {
               ++stack.last().second;
               const int child = graph.out[node][edge];
               if (state[child] == 1)
               {
                  continue;
               }
               acyclic[node] << child;
               if (state[child] == 0)
               {
                  state[child] = 1;
                  stack.append({child, 0});
               }
            }
            else
            {
               state[node] = 2;
               stack.removeLast();
            }
         }
      }

      QVector<int> inDegree(graph.size, 0);
      for (const QVector<int>& children : acyclic)
      {
         for (int child : children)
         {
            ++inDegree[child];
         }
      }
      QVector<int> layer(graph.size, 0);
      QVector<int> queue;
      for (int i = 0; i < graph.size; ++i)
      {
         if (inDegree[i] == 0)
         {
            queue << i;
         }
      }
      for (int head = 0; head < queue.size(); ++head)
      {
         const int node = queue[head];
         for (int child : acyclic[node])
         {
            layer[child] = std::max(layer[child], layer[node] + 1);
            if (--inDegree[child] == 0)
            {
               queue << child;
            }
         }
      }

      // a parent right before its first child keeps lines short
      for (int i = queue.size() - 1; i >= 0; --i)
      {
         const int node = queue[i];
         if (acyclic[node].isEmpty())
         {
            continue;
         }
         int nearest = INT_MAX;
         for (int child : acyclic[node])
         {
            nearest = std::min(nearest, layer[child]);
         }
         layer[node] = std::max(layer[node], nearest - 1);
      }
      return layer;
   }

   qreal barycenter(int node, const Graph& graph, const QHash<int, int>& positionInLayer, const QVector<int>& layer, int otherLayer)
   {
      qreal sum = 0;
      int count = 0;
      for (int neighbour : graph.neighbours[node])
      {
         if (layer[neighbour] == otherLayer)
         {
            sum += positionInLayer.value(neighbour);
            ++count;
         }
      }
      return count == 0 ? -1 : sum / count;
   }

   // Top left corners of the nodes: related ones in columns, unrelated ones in a grid below
   QVector<QPointF> arrangeGraph(const Graph& graph, const QVector<QSizeF>& sizes)
   {
      QVector<QPointF> positions(graph.size);
      QVector<int> connected;
      QVector<int> isolated;
      for (int i = 0; i < graph.size; ++i)
      {
         (graph.neighbours[i].isEmpty() ? isolated : connected) << i;
      }

      const QVector<int> layer = layers(graph);
      int layerCount = 0;
      for (int node : qAsConst(connected))
      {
         layerCount = std::max(layerCount, layer[node] + 1);
      }

      QVector<QVector<int>> columns(layerCount);
      for (int node : qAsConst(connected))
      {
         columns[layer[node]] << node;
      }

      QHash<int, int> positionInLayer;
      auto updatePositions = [&]() {
         for (const QVector<int>& column : qAsConst(columns))
         {
            for (int i = 0; i < column.size(); ++i)
            {
               positionInLayer[column[i]] = i;
            }
         }
      };
      updatePositions();

      // barycenter sweeps, left to right then right to left
      for (int sweep = 0; sweep < 6; ++sweep)
      {
         const bool forward = sweep % 2 == 0;
         for (int step = 1; step < layerCount; ++step)
         {
            const int current = forward ? step : layerCount - 1 - step;
            const int other = forward ? current - 1 : current + 1;
            QVector<int>& column = columns[current];
            QHash<int, qreal> keys;
            for (int i = 0; i < column.size(); ++i)
            {
               const qreal key = barycenter(column[i], graph, positionInLayer, layer, other);
               keys[column[i]] = key < 0 ? i : key;
            }
            std::stable_sort(column.begin(), column.end(), [&](int a, int b) {return keys[a] < keys[b];});
            updatePositions();
         }
      }

      QVector<qreal> columnHeights(layerCount, 0);
      QVector<qreal> columnWidths(layerCount, 0);
      for (int c = 0; c < layerCount; ++c)
      {
         for (int node : qAsConst(columns[c]))
         {
            columnHeights[c] += sizes[node].height() + RowGap;
            columnWidths[c] = std::max(columnWidths[c], sizes[node].width());
         }
         columnHeights[c] = std::max<qreal>(0, columnHeights[c] - RowGap);
      }
      const qreal tallest = layerCount == 0 ? 0 : *std::max_element(columnHeights.cbegin(), columnHeights.cend());

      qreal x = 0;
      qreal graphWidth = 0;
      for (int c = 0; c < layerCount; ++c)
      {
         qreal y = (tallest - columnHeights[c]) / 2;
         for (int node : qAsConst(columns[c]))
         {
            positions[node] = QPointF(x, std::round(y));
            y += sizes[node].height() + RowGap;
         }
         x += columnWidths[c] + ColumnGap;
         graphWidth = x - ColumnGap;
      }

      if (!isolated.isEmpty())
      {
         // rows about as wide as the graph above, or a square grid without one
         qreal averageWidth = 0;
         for (int node : qAsConst(isolated))
         {
            averageWidth += sizes[node].width();
         }
         averageWidth /= isolated.size();
         const int perRow = std::max(1, connected.isEmpty() ? int(std::ceil(std::sqrt(double(isolated.size()))))
                                                            : int(graphWidth / (averageWidth + GridGap)));
         qreal top = connected.isEmpty() ? 0 : tallest + GridGap * 2;
         for (int start = 0; start < isolated.size(); start += perRow)
         {
            qreal left = 0;
            qreal rowHeight = 0;
            for (int i = start; i < std::min(start + perRow, isolated.size()); ++i)
            {
               positions[isolated[i]] = QPointF(left, top);
               left += sizes[isolated[i]].width() + GridGap;
               rowHeight = std::max(rowHeight, sizes[isolated[i]].height());
            }
            top += rowHeight + GridGap;
         }
      }
      return positions;
   }
}

QMap<QString, QPointF> AutoLayout::arrange(const SchemaDTO &schema, const QMap<QString, QSizeF> &sizes)
{
   QMap<QString, QPointF> result;
   const int tableCount = schema.tables.size();
   if (tableCount == 0)
   {
      return result;
   }

   // A table group is arranged on its own and then placed as one block, so its frame holds only its tables
   QVector<int> groupOfTable(tableCount, -1);
   QVector<QVector<int>> members;
   for (const TableGroupDTO& group : schema.tableGroups)
   {
      QVector<int> tables;
      for (const QString& name : group.tables)
      {
         const int index = schema.tableIndex(name);
         if (index >= 0 && groupOfTable[index] < 0)
         {
            groupOfTable[index] = members.size();
            tables << index;
         }
      }
      if (!tables.isEmpty())
      {
         members << tables;
      }
   }

   auto sizeOf = [&](int table) {
      return sizes.value(schema.tables[table].fullName(), QSizeF(220, 160));
   };

   // positions inside each group, relative to the block's top left corner
   QVector<QPointF> offsetInBlock(tableCount);
   QVector<QSizeF> groupSizes;
   for (const QVector<int>& tables : qAsConst(members))
   {
      QHash<int, int> local;
      for (int i = 0; i < tables.size(); ++i)
      {
         local.insert(tables[i], i);
      }
      Graph graph(tables.size());
      for (const RefDTO& ref : schema.refs)
      {
         const int left = local.value(schema.tableIndex(ref.left.tableFullName()), -1);
         const int right = local.value(schema.tableIndex(ref.right.tableFullName()), -1);
         const auto edge = parentAndChild(ref, left, right);
         graph.addEdge(edge.first, edge.second);
      }
      QVector<QSizeF> localSizes;
      for (int table : tables)
      {
         localSizes << sizeOf(table);
      }
      const QVector<QPointF> positions = arrangeGraph(graph, localSizes);
      QRectF bounds;
      for (int i = 0; i < tables.size(); ++i)
      {
         const QRectF rect(positions[i], localSizes[i]);
         bounds = bounds.isNull() ? rect : bounds.united(rect);
      }
      for (int i = 0; i < tables.size(); ++i)
      {
         offsetInBlock[tables[i]] = positions[i] - bounds.topLeft() + QPointF(GroupPadding, GroupPadding + GroupTitle);
      }
      groupSizes << QSizeF(bounds.width() + GroupPadding * 2, bounds.height() + GroupPadding * 2 + GroupTitle);
   }

   // blocks: the groups first, then the tables outside of groups
   QVector<int> blockOfTable(tableCount);
   QVector<QSizeF> blockSizes = groupSizes;
   for (int table = 0; table < tableCount; ++table)
   {
      if (groupOfTable[table] >= 0)
      {
         blockOfTable[table] = groupOfTable[table];
      }
      else
      {
         blockOfTable[table] = blockSizes.size();
         blockSizes << sizeOf(table);
      }
   }

   Graph blocks(blockSizes.size());
   for (const RefDTO& ref : schema.refs)
   {
      const int left = schema.tableIndex(ref.left.tableFullName());
      const int right = schema.tableIndex(ref.right.tableFullName());
      if (left < 0 || right < 0)
      {
         continue;
      }
      const auto edge = parentAndChild(ref, blockOfTable[left], blockOfTable[right]);
      blocks.addEdge(edge.first, edge.second);
   }
   const QVector<QPointF> blockPositions = arrangeGraph(blocks, blockSizes);

   for (int table = 0; table < tableCount; ++table)
   {
      const QPointF block = blockPositions[blockOfTable[table]];
      result.insert(schema.tables[table].fullName(), groupOfTable[table] >= 0 ? block + offsetInBlock[table] : block);
   }
   return result;
}
