#include "DbmlEditor.h"
#include "DbmlHighlighter.h"
#include "Theme.h"
#include <QPainter>
#include <QTextBlock>
#include <QToolTip>
#include <QHelpEvent>
#include <QKeyEvent>
#include <QWheelEvent>
#include <QScrollBar>
#include <QMap>

namespace
{
   const QString Indent = QStringLiteral("  ");

   class Gutter : public QWidget
   {
   public:
      explicit Gutter(DbmlEditor* editor) : QWidget(editor), m_editor(editor) {}

      QSize sizeHint() const override
      {
         return QSize(m_editor->gutterWidth(), 0);
      }

   protected:
      void paintEvent(QPaintEvent* event) override
      {
         m_editor->paintGutter(event);
      }

      bool event(QEvent* event) override
      {
         if (event->type() == QEvent::ToolTip)
         {
            auto help = static_cast<QHelpEvent*>(event);
            m_editor->showErrorToolTip(help->pos(), help->globalPos());
            return true;
         }
         return QWidget::event(event);
      }

   private:
      DbmlEditor* m_editor;
   };

   int leadingSpaces(const QString& text)
   {
      int count = 0;
      while (count < text.size() && (text[count] == ' ' || text[count] == '\t'))
      {
         ++count;
      }
      return count;
   }
}

struct DbmlEditor::Impl
{
   explicit Impl(DbmlEditor* editor) : editor(editor) {}

   void updateGutterWidth();
   void updateGutter(const QRect&, int dy);
   void updateExtraSelections();
   bool handleEnter();
   bool handleClosingBrace();
   bool handleBracket(QKeyEvent*);
   bool handleTab(bool backward);

   DbmlEditor* editor = nullptr;
   Gutter* gutter = nullptr;
   DbmlHighlighter* highlighter = nullptr;
   Theme theme = Theme::light();
   QVector<DbmlError> errors;
   QMap<int, QStringList> errorsByLine;
   QList<QTextEdit::ExtraSelection> searchSelections;
};

DbmlEditor::DbmlEditor(QWidget *parent)
   : QPlainTextEdit(parent)
{
   pimpl = std::make_unique<Impl>(this);
   pimpl->gutter = new Gutter(this);
   pimpl->highlighter = new DbmlHighlighter(document());

   setLineWrapMode(QPlainTextEdit::NoWrap);
   setFrameShape(QFrame::NoFrame);
   setTabChangesFocus(false);
   setCenterOnScroll(false);

   QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
   font.setPointSizeF(std::max(10.5, font.pointSizeF()));
   setEditorFont(font);

   connect(this, &QPlainTextEdit::blockCountChanged, this, [this]() {pimpl->updateGutterWidth();});
   connect(this, &QPlainTextEdit::updateRequest, this, [this](const QRect& rect, int dy) {pimpl->updateGutter(rect, dy);});
   connect(this, &QPlainTextEdit::cursorPositionChanged, this, [this]() {pimpl->updateExtraSelections();});
   pimpl->updateGutterWidth();
   pimpl->updateExtraSelections();
}

DbmlEditor::~DbmlEditor() = default;

void DbmlEditor::setErrors(const QVector<DbmlError> &errors)
{
   pimpl->errors = errors;
   pimpl->errorsByLine.clear();
   for (const DbmlError& error : errors)
   {
      pimpl->errorsByLine[error.line] << error.message;
   }
   pimpl->updateExtraSelections();
   pimpl->gutter->update();
}

void DbmlEditor::setTheme(const Theme &theme)
{
   pimpl->theme = theme;
   QPalette palette = this->palette();
   palette.setColor(QPalette::Base, theme.editorBackground);
   palette.setColor(QPalette::Text, theme.editorText);
   palette.setColor(QPalette::Highlight, theme.dark ? QColor("#264f78") : QColor("#b4d5fe"));
   palette.setColor(QPalette::HighlightedText, theme.editorText);
   setPalette(palette);
   pimpl->highlighter->setTheme(theme);
   pimpl->updateExtraSelections();
   pimpl->gutter->update();
}

void DbmlEditor::setEditorFont(const QFont &font)
{
   setFont(font);
   setTabStopDistance(QFontMetricsF(font).horizontalAdvance(' ') * 2);
   pimpl->updateGutterWidth();
   emit fontSizeChanged(font.pointSizeF());
}

void DbmlEditor::zoomText(int steps)
{
   QFont current = font();
   current.setPointSizeF(std::clamp(current.pointSizeF() + steps, 7.0, 32.0));
   setEditorFont(current);
}

int DbmlEditor::currentLine() const
{
   return textCursor().blockNumber() + 1;
}

void DbmlEditor::goToLine(int line)
{
   const QTextBlock block = document()->findBlockByNumber(std::max(0, line - 1));
   if (!block.isValid())
   {
      return;
   }
   QTextCursor cursor(block);
   cursor.movePosition(QTextCursor::StartOfBlock);
   // to the first character of the line, after the indentation
   cursor.movePosition(QTextCursor::Right, QTextCursor::MoveAnchor, leadingSpaces(block.text()));
   setTextCursor(cursor);
   // the line goes to the upper third of the editor if it is not visible
   const QRectF rect = blockBoundingGeometry(block).translated(contentOffset());
   if (rect.top() < 0 || rect.bottom() > viewport()->height())
   {
      const int visibleLines = viewport()->height() / std::max(1, fontMetrics().height());
      verticalScrollBar()->setValue(std::max(0, line - 1 - visibleLines / 3));
   }
}

void DbmlEditor::toggleComment()
{
   QTextCursor cursor = textCursor();
   const int startBlock = document()->findBlock(cursor.selectionStart()).blockNumber();
   int endBlock = document()->findBlock(cursor.selectionEnd()).blockNumber();
   if (cursor.hasSelection() && document()->findBlock(cursor.selectionEnd()).position() == cursor.selectionEnd() && endBlock > startBlock)
   {
      --endBlock; // a selection ending at the start of a line does not include it
   }

   bool allCommented = true;
   int indent = INT_MAX;
   for (int i = startBlock; i <= endBlock; ++i)
   {
      const QString text = document()->findBlockByNumber(i).text();
      if (text.trimmed().isEmpty())
      {
         continue;
      }
      indent = std::min(indent, leadingSpaces(text));
      allCommented = allCommented && text.trimmed().startsWith(QLatin1String("//"));
   }
   if (indent == INT_MAX)
   {
      return;
   }

   cursor.beginEditBlock();
   for (int i = startBlock; i <= endBlock; ++i)
   {
      const QTextBlock block = document()->findBlockByNumber(i);
      const QString text = block.text();
      if (text.trimmed().isEmpty())
      {
         continue;
      }
      QTextCursor line(block);
      if (allCommented)
      {
         const int at = text.indexOf(QLatin1String("//"));
         line.setPosition(block.position() + at);
         const int length = text.midRef(at, 3) == QLatin1String("// ") ? 3 : 2;
         line.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor, length);
         line.removeSelectedText();
      }
      else
      {
         line.setPosition(block.position() + indent);
         line.insertText(QStringLiteral("// "));
      }
   }
   cursor.endEditBlock();
}

int DbmlEditor::setSearchHighlight(const QString &text, bool caseSensitive)
{
   pimpl->searchSelections.clear();
   int count = 0;
   if (!text.isEmpty())
   {
      QTextDocument::FindFlags flags;
      if (caseSensitive)
      {
         flags |= QTextDocument::FindCaseSensitively;
      }
      QTextCursor found = document()->find(text, 0, flags);
      while (!found.isNull() && count < 5000)
      {
         QTextEdit::ExtraSelection selection;
         selection.cursor = found;
         selection.format.setBackground(pimpl->theme.findMatch);
         pimpl->searchSelections << selection;
         ++count;
         found = document()->find(text, found, flags);
      }
   }
   pimpl->updateExtraSelections();
   return count;
}

int DbmlEditor::gutterWidth() const
{
   const int digits = QString::number(std::max(1, blockCount())).size();
   return 16 + fontMetrics().horizontalAdvance(QLatin1Char('9')) * std::max(3, digits) + 10;
}

void DbmlEditor::Impl::updateGutterWidth()
{
   editor->setViewportMargins(editor->gutterWidth(), 0, 0, 0);
}

void DbmlEditor::Impl::updateGutter(const QRect &rect, int dy)
{
   if (dy != 0)
   {
      gutter->scroll(0, dy);
   }
   else
   {
      gutter->update(0, rect.y(), gutter->width(), rect.height());
   }
   if (rect.contains(editor->viewport()->rect()))
   {
      updateGutterWidth();
   }
}

void DbmlEditor::paintGutter(QPaintEvent *event)
{
   QPainter painter(pimpl->gutter);
   painter.fillRect(event->rect(), pimpl->theme.gutterBackground);
   painter.setFont(font());

   const int current = textCursor().blockNumber();
   QTextBlock block = firstVisibleBlock();
   int number = block.blockNumber();
   int top = qRound(blockBoundingGeometry(block).translated(contentOffset()).top());
   int bottom = top + qRound(blockBoundingRect(block).height());
   const int width = pimpl->gutter->width();
   while (block.isValid() && top <= event->rect().bottom())
   {
      if (block.isVisible() && bottom >= event->rect().top())
      {
         const bool hasError = pimpl->errorsByLine.contains(number + 1);
         if (hasError)
         {
            painter.setRenderHint(QPainter::Antialiasing);
            painter.setPen(Qt::NoPen);
            painter.setBrush(pimpl->theme.error);
            painter.drawEllipse(QPointF(8, top + fontMetrics().height() / 2.0), 3.5, 3.5);
         }
         QColor color = pimpl->theme.gutterText;
         if (number == current)
         {
            color = pimpl->theme.editorText;
         }
         painter.setPen(hasError ? pimpl->theme.error : color);
         painter.drawText(0, top, width - 8, fontMetrics().height(), Qt::AlignRight, QString::number(number + 1));
      }
      block = block.next();
      top = bottom;
      bottom = top + qRound(blockBoundingRect(block).height());
      ++number;
   }
}

void DbmlEditor::Impl::updateExtraSelections()
{
   QList<QTextEdit::ExtraSelection> selections;

   QTextEdit::ExtraSelection currentLine;
   currentLine.format.setBackground(theme.currentLine);
   currentLine.format.setProperty(QTextFormat::FullWidthSelection, true);
   currentLine.cursor = editor->textCursor();
   currentLine.cursor.clearSelection();
   selections << currentLine;

   selections << searchSelections;

   for (const DbmlError& error : qAsConst(errors))
   {
      const QTextBlock block = editor->document()->findBlockByNumber(error.line - 1);
      if (!block.isValid())
      {
         continue;
      }
      QTextEdit::ExtraSelection selection;
      selection.format.setUnderlineStyle(QTextCharFormat::WaveUnderline);
      selection.format.setUnderlineColor(theme.error);
      QTextCursor cursor(block);
      if (error.length <= 0)
      {
         // the whole line from the first character
         cursor.movePosition(QTextCursor::Right, QTextCursor::MoveAnchor, leadingSpaces(block.text()));
         cursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
      }
      else
      {
         const int column = std::min(std::max(0, error.column - 1), block.length() - 1);
         cursor.movePosition(QTextCursor::Right, QTextCursor::MoveAnchor, column);
         const int length = std::max(1, std::min(error.length, block.length() - 1 - column));
         if (column >= block.length() - 1)
         {
            // at the end of the line: underline the last character
            cursor.movePosition(QTextCursor::Left, QTextCursor::MoveAnchor, column > 0 ? 1 : 0);
            cursor.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor, 1);
         }
         else
         {
            cursor.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor, length);
         }
      }
      selection.cursor = cursor;
      selections << selection;
   }
   editor->setExtraSelections(selections);
   gutter->update();
}

void DbmlEditor::showErrorToolTip(const QPoint &viewportPos, const QPoint &globalPos)
{
   const int line = cursorForPosition(QPoint(0, viewportPos.y())).blockNumber() + 1;
   if (pimpl->errorsByLine.contains(line))
   {
      QToolTip::showText(globalPos, pimpl->errorsByLine[line].join('\n'), this);
   }
   else
   {
      QToolTip::hideText();
   }
}

bool DbmlEditor::viewportEvent(QEvent *event)
{
   if (event->type() == QEvent::ToolTip)
   {
      auto help = static_cast<QHelpEvent*>(event);
      showErrorToolTip(help->pos(), help->globalPos());
      return true;
   }
   return QPlainTextEdit::viewportEvent(event);
}

void DbmlEditor::resizeEvent(QResizeEvent *event)
{
   QPlainTextEdit::resizeEvent(event);
   const QRect contents = contentsRect();
   pimpl->gutter->setGeometry(QRect(contents.left(), contents.top(), gutterWidth(), contents.height()));
}

bool DbmlEditor::Impl::handleEnter()
{
   QTextCursor cursor = editor->textCursor();
   const QString line = cursor.block().text();
   const int column = cursor.positionInBlock();
   QString indent = line.left(leadingSpaces(line));
   const QString before = line.left(column).trimmed();
   const QString after = line.mid(column).trimmed();

   cursor.beginEditBlock();
   if ((before.endsWith('{') && after.startsWith('}')) || (before.endsWith('[') && after.startsWith(']')))
   {
      // between brackets: the cursor goes to an indented line, the closing one below it
      cursor.insertText('\n' + indent + Indent);
      const int position = cursor.position();
      cursor.insertText('\n' + indent);
      cursor.setPosition(position);
   }
   else
   {
      if (before.endsWith('{') || before.endsWith('['))
      {
         indent += Indent;
      }
      cursor.insertText('\n' + indent);
   }
   cursor.endEditBlock();
   editor->setTextCursor(cursor);
   editor->ensureCursorVisible();
   return true;
}

bool DbmlEditor::Impl::handleClosingBrace()
{
   // "}" typed on a line of spaces goes one level back
   QTextCursor cursor = editor->textCursor();
   const QString line = cursor.block().text();
   if (!line.left(cursor.positionInBlock()).trimmed().isEmpty() || cursor.positionInBlock() < Indent.size())
   {
      return false;
   }
   cursor.beginEditBlock();
   cursor.movePosition(QTextCursor::Left, QTextCursor::KeepAnchor, Indent.size());
   cursor.insertText(QStringLiteral("}"));
   cursor.endEditBlock();
   editor->setTextCursor(cursor);
   return true;
}

bool DbmlEditor::Impl::handleBracket(QKeyEvent *event)
{
   static const QMap<QChar, QChar> pairs{{'{', '}'}, {'[', ']'}, {'(', ')'}};
   const QString text = event->text();
   if (text.size() != 1)
   {
      return false;
   }
   const QChar c = text[0];
   QTextCursor cursor = editor->textCursor();
   const QString line = cursor.block().text();
   const int column = cursor.positionInBlock();
   const QChar next = column < line.size() ? line[column] : QChar();

   // typing the closing bracket that is already there steps over it
   if ((c == '}' || c == ']' || c == ')') && next == c && !cursor.hasSelection())
   {
      cursor.movePosition(QTextCursor::Right);
      editor->setTextCursor(cursor);
      return true;
   }
   if (!pairs.contains(c))
   {
      return false;
   }
   if (cursor.hasSelection())
   {
      const QString selected = cursor.selectedText();
      cursor.insertText(c + selected + pairs[c]);
      return true;
   }
   // only before the end of a line, a space or a closing bracket
   if (!next.isNull() && !next.isSpace() && next != '}' && next != ']' && next != ')' && next != ',')
   {
      return false;
   }
   cursor.insertText(QString(c) + pairs[c]);
   cursor.movePosition(QTextCursor::Left);
   editor->setTextCursor(cursor);
   return true;
}

bool DbmlEditor::Impl::handleTab(bool backward)
{
   QTextCursor cursor = editor->textCursor();
   if (!cursor.hasSelection() && !backward)
   {
      cursor.insertText(Indent);
      return true;
   }
   const int startBlock = editor->document()->findBlock(cursor.selectionStart()).blockNumber();
   const int endBlock = editor->document()->findBlock(cursor.selectionEnd()).blockNumber();
   cursor.beginEditBlock();
   for (int i = startBlock; i <= endBlock; ++i)
   {
      const QTextBlock block = editor->document()->findBlockByNumber(i);
      QTextCursor line(block);
      if (backward)
      {
         const int spaces = std::min<int>(Indent.size(), leadingSpaces(block.text()));
         line.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor, spaces);
         line.removeSelectedText();
      }
      else if (!block.text().trimmed().isEmpty())
      {
         line.insertText(Indent);
      }
   }
   cursor.endEditBlock();
   return true;
}

void DbmlEditor::keyPressEvent(QKeyEvent *event)
{
   if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) && !(event->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier)))
   {
      pimpl->handleEnter();
      return;
   }
   if (event->key() == Qt::Key_Tab && !(event->modifiers() & Qt::ControlModifier))
   {
      pimpl->handleTab(false);
      return;
   }
   if (event->key() == Qt::Key_Backtab)
   {
      pimpl->handleTab(true);
      return;
   }
   if (event->key() == Qt::Key_Backspace && !textCursor().hasSelection())
   {
      // removes the empty pair "{}" at once, or a whole indentation step
      QTextCursor cursor = textCursor();
      const QString line = cursor.block().text();
      const int column = cursor.positionInBlock();
      if (column > 0 && column < line.size())
      {
         const QString pair = line.mid(column - 1, 2);
         if (pair == QLatin1String("{}") || pair == QLatin1String("[]") || pair == QLatin1String("()"))
         {
            cursor.movePosition(QTextCursor::Left);
            cursor.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor, 2);
            cursor.removeSelectedText();
            return;
         }
      }
      if (column >= Indent.size() && line.left(column).trimmed().isEmpty() && column % Indent.size() == 0)
      {
         cursor.movePosition(QTextCursor::Left, QTextCursor::KeepAnchor, Indent.size());
         cursor.removeSelectedText();
         return;
      }
   }
   if (event->text() == QLatin1String("}") && pimpl->handleClosingBrace())
   {
      return;
   }
   if (!(event->modifiers() & (Qt::ControlModifier | Qt::AltModifier)) && pimpl->handleBracket(event))
   {
      return;
   }
   QPlainTextEdit::keyPressEvent(event);
}

void DbmlEditor::wheelEvent(QWheelEvent *event)
{
   if (event->modifiers() & Qt::ControlModifier)
   {
      const int steps = event->angleDelta().y() > 0 ? 1 : event->angleDelta().y() < 0 ? -1 : 0;
      if (steps != 0)
      {
         zoomText(steps);
      }
      event->accept();
      return;
   }
   QPlainTextEdit::wheelEvent(event);
}
