#include "FindBar.h"
#include "DbmlEditor.h"
#include <QApplication>
#include <QLineEdit>
#include <QToolButton>
#include <QCheckBox>
#include <QLabel>
#include <QGridLayout>
#include <QKeyEvent>
#include <QTextDocument>
#include <QTextCursor>

struct FindBar::Impl
{
   explicit Impl(FindBar* bar) : bar(bar) {}

   bool find(bool backward);
   QTextDocument::FindFlags flags(bool backward) const;
   void updateMatches();
   void replace();
   void replaceAll();
   void close();

   FindBar* bar = nullptr;
   DbmlEditor* editor = nullptr;
   QLineEdit* findEdit = nullptr;
   QLineEdit* replaceEdit = nullptr;
   QCheckBox* caseSensitive = nullptr;
   QLabel* status = nullptr;
   QWidget* replaceRow = nullptr;
};

FindBar::FindBar(DbmlEditor *editor, QWidget *parent)
   : QWidget(parent)
{
   pimpl = std::make_unique<Impl>(this);
   pimpl->editor = editor;

   auto layout = new QGridLayout(this);
   layout->setContentsMargins(6, 4, 6, 4);
   layout->setHorizontalSpacing(4);
   layout->setVerticalSpacing(4);

   pimpl->findEdit = new QLineEdit(this);
   pimpl->findEdit->setPlaceholderText(tr("Find"));
   pimpl->findEdit->setClearButtonEnabled(true);
   auto previous = new QToolButton(this);
   previous->setText(QStringLiteral("▲"));
   previous->setToolTip(tr("Previous match (Shift+Enter)"));
   auto next = new QToolButton(this);
   next->setText(QStringLiteral("▼"));
   next->setToolTip(tr("Next match (Enter)"));
   pimpl->caseSensitive = new QCheckBox(tr("Aa"), this);
   pimpl->caseSensitive->setToolTip(tr("Match case"));
   pimpl->status = new QLabel(this);
   pimpl->status->setMinimumWidth(70);
   auto close = new QToolButton(this);
   close->setText(QStringLiteral("✕"));
   close->setToolTip(tr("Close (Escape)"));
   close->setAutoRaise(true);

   layout->addWidget(pimpl->findEdit, 0, 0);
   layout->addWidget(previous, 0, 1);
   layout->addWidget(next, 0, 2);
   layout->addWidget(pimpl->caseSensitive, 0, 3);
   layout->addWidget(pimpl->status, 0, 4);
   layout->addWidget(close, 0, 5);

   pimpl->replaceRow = new QWidget(this);
   auto replaceLayout = new QHBoxLayout(pimpl->replaceRow);
   replaceLayout->setContentsMargins(0, 0, 0, 0);
   replaceLayout->setSpacing(4);
   pimpl->replaceEdit = new QLineEdit(pimpl->replaceRow);
   pimpl->replaceEdit->setPlaceholderText(tr("Replace"));
   auto replaceButton = new QToolButton(pimpl->replaceRow);
   replaceButton->setText(tr("Replace"));
   auto replaceAllButton = new QToolButton(pimpl->replaceRow);
   replaceAllButton->setText(tr("All"));
   replaceAllButton->setToolTip(tr("Replace all"));
   replaceLayout->addWidget(pimpl->replaceEdit, 1);
   replaceLayout->addWidget(replaceButton);
   replaceLayout->addWidget(replaceAllButton);
   layout->addWidget(pimpl->replaceRow, 1, 0, 1, 6);
   layout->setColumnStretch(0, 1);

   connect(pimpl->findEdit, &QLineEdit::textChanged, this, [this]() {
      // search from the start of the current selection so the match grows while typing
      QTextCursor cursor = pimpl->editor->textCursor();
      cursor.setPosition(cursor.selectionStart());
      pimpl->editor->setTextCursor(cursor);
      pimpl->find(false);
      pimpl->updateMatches();
   });
   connect(pimpl->findEdit, &QLineEdit::returnPressed, this, &FindBar::findNext);
   connect(pimpl->replaceEdit, &QLineEdit::returnPressed, this, [this]() {pimpl->replace();});
   connect(pimpl->caseSensitive, &QCheckBox::toggled, this, [this]() {pimpl->updateMatches();});
   connect(previous, &QToolButton::clicked, this, &FindBar::findPrevious);
   connect(next, &QToolButton::clicked, this, &FindBar::findNext);
   connect(replaceButton, &QToolButton::clicked, this, [this]() {pimpl->replace();});
   connect(replaceAllButton, &QToolButton::clicked, this, [this]() {pimpl->replaceAll();});
   connect(close, &QToolButton::clicked, this, [this]() {pimpl->close();});
   connect(editor->document(), &QTextDocument::contentsChanged, this, [this]() {
      if (isVisible())
      {
         pimpl->updateMatches();
      }
   });

   hide();
}

FindBar::~FindBar() = default;

QTextDocument::FindFlags FindBar::Impl::flags(bool backward) const
{
   QTextDocument::FindFlags result;
   if (backward)
   {
      result |= QTextDocument::FindBackward;
   }
   if (caseSensitive->isChecked())
   {
      result |= QTextDocument::FindCaseSensitively;
   }
   return result;
}

bool FindBar::Impl::find(bool backward)
{
   const QString text = findEdit->text();
   if (text.isEmpty())
   {
      return false;
   }
   if (editor->find(text, flags(backward)))
   {
      return true;
   }
   // wrap around
   QTextCursor cursor = editor->textCursor();
   cursor.movePosition(backward ? QTextCursor::End : QTextCursor::Start);
   editor->setTextCursor(cursor);
   return editor->find(text, flags(backward));
}

void FindBar::Impl::updateMatches()
{
   const QString text = findEdit->text();
   const int count = editor->setSearchHighlight(text, caseSensitive->isChecked());
   if (text.isEmpty())
   {
      status->clear();
      findEdit->setStyleSheet(QString());
      return;
   }
   status->setText(count == 0 ? tr("No results") : (count == 1 ? tr("1 match") : tr("%1 matches").arg(count)));
   findEdit->setStyleSheet(count == 0 ? QStringLiteral("QLineEdit { color: #e53935; }") : QString());
}

void FindBar::Impl::replace()
{
   const QString text = findEdit->text();
   QTextCursor cursor = editor->textCursor();
   const Qt::CaseSensitivity sensitivity = caseSensitive->isChecked() ? Qt::CaseSensitive : Qt::CaseInsensitive;
   if (cursor.hasSelection() && cursor.selectedText().compare(text, sensitivity) == 0)
   {
      cursor.insertText(replaceEdit->text());
   }
   find(false);
}

void FindBar::Impl::replaceAll()
{
   const QString text = findEdit->text();
   if (text.isEmpty())
   {
      return;
   }
   QTextCursor cursor(editor->document());
   cursor.beginEditBlock();
   QTextDocument::FindFlags searchFlags = flags(false);
   QTextCursor found = editor->document()->find(text, 0, searchFlags);
   while (!found.isNull())
   {
      found.insertText(replaceEdit->text());
      found = editor->document()->find(text, found, searchFlags);
   }
   cursor.endEditBlock();
}

void FindBar::Impl::close()
{
   editor->setSearchHighlight(QString(), false);
   bar->hide();
   editor->setFocus();
}

void FindBar::showFind()
{
   pimpl->replaceRow->hide();
   show();
   const QString selected = pimpl->editor->textCursor().selectedText();
   if (!selected.isEmpty() && !selected.contains(QChar::ParagraphSeparator))
   {
      pimpl->findEdit->setText(selected);
   }
   pimpl->findEdit->setFocus();
   pimpl->findEdit->selectAll();
   pimpl->updateMatches();
}

void FindBar::showReplace()
{
   showFind();
   pimpl->replaceRow->show();
}

void FindBar::findNext()
{
   if (isHidden())
   {
      showFind();
      return;
   }
   const bool backward = QApplication::keyboardModifiers() & Qt::ShiftModifier;
   pimpl->find(backward);
}

void FindBar::findPrevious()
{
   if (isHidden())
   {
      showFind();
      return;
   }
   pimpl->find(true);
}

void FindBar::keyPressEvent(QKeyEvent *event)
{
   if (event->key() == Qt::Key_Escape)
   {
      pimpl->close();
      return;
   }
   QWidget::keyPressEvent(event);
}
