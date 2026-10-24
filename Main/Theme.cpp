#include "Theme.h"

Theme Theme::light()
{
   Theme theme;
   theme.dark = false;

   theme.canvas = QColor("#f4f6f8");
   theme.gridDot = QColor("#d5dae0");
   theme.tableBody = QColor("#ffffff");
   theme.tableBorder = QColor("#c9d1da");
   theme.tableShadow = QColor(0, 0, 0, 28);
   theme.defaultHeader = QColor("#316896");
   theme.columnText = QColor("#1f2933");
   theme.typeText = QColor("#7b8794");
   theme.rowSeparator = QColor("#edf0f3");
   theme.rowHover = QColor("#eef4fb");
   theme.rowHighlight = QColor("#d9ecff");
   theme.keyIcon = QColor("#d4a017");
   theme.noteMark = QColor("#9aa5b1");
   theme.refLine = QColor("#8492a6");
   theme.refHighlight = QColor("#1e88e5");
   theme.selection = QColor("#1e88e5");
   theme.groupDefault = QColor("#7f8c8d");
   theme.noteBody = QColor("#fff8c5");
   theme.noteHeader = QColor("#f5d84d");
   theme.noteText = QColor("#3e3a1f");

   theme.editorBackground = QColor("#ffffff");
   theme.editorText = QColor("#24292e");
   theme.gutterBackground = QColor("#f6f8fa");
   theme.gutterText = QColor("#959da5");
   theme.currentLine = QColor("#f1f8ff");
   theme.keyword = QColor("#d73a49");
   theme.setting = QColor("#6f42c1");
   theme.type = QColor("#005cc5");
   theme.string = QColor("#22863a");
   theme.expression = QColor("#e36209");
   theme.comment = QColor("#6a737d");
   theme.number = QColor("#005cc5");
   theme.symbol = QColor("#d73a49");
   theme.error = QColor("#e53935");
   theme.findMatch = QColor("#ffe58f");
   return theme;
}

Theme Theme::darkTheme()
{
   Theme theme;
   theme.dark = true;

   theme.canvas = QColor("#1b1f24");
   theme.gridDot = QColor("#2e353d");
   theme.tableBody = QColor("#262c33");
   theme.tableBorder = QColor("#3a434d");
   theme.tableShadow = QColor(0, 0, 0, 90);
   theme.defaultHeader = QColor("#2b5a82");
   theme.columnText = QColor("#e1e6eb");
   theme.typeText = QColor("#8b98a5");
   theme.rowSeparator = QColor("#2f363e");
   theme.rowHover = QColor("#2f3a46");
   theme.rowHighlight = QColor("#1f4466");
   theme.keyIcon = QColor("#f0c040");
   theme.noteMark = QColor("#6b7785");
   theme.refLine = QColor("#6b7a8c");
   theme.refHighlight = QColor("#4fa3ff");
   theme.selection = QColor("#4fa3ff");
   theme.groupDefault = QColor("#95a5a6");
   theme.noteBody = QColor("#4a4425");
   theme.noteHeader = QColor("#7a6a1e");
   theme.noteText = QColor("#f4edc4");

   theme.editorBackground = QColor("#1e2228");
   theme.editorText = QColor("#d7dce2");
   theme.gutterBackground = QColor("#1a1d22");
   theme.gutterText = QColor("#5c6773");
   theme.currentLine = QColor("#262b33");
   theme.keyword = QColor("#ff7b72");
   theme.setting = QColor("#d2a8ff");
   theme.type = QColor("#79c0ff");
   theme.string = QColor("#a5d6a7");
   theme.expression = QColor("#ffa657");
   theme.comment = QColor("#8b949e");
   theme.number = QColor("#79c0ff");
   theme.symbol = QColor("#ff7b72");
   theme.error = QColor("#ff6b6b");
   theme.findMatch = QColor("#6e5a12");
   return theme;
}

QPalette Theme::palette() const
{
   if (!dark)
   {
      QPalette palette;
      palette.setColor(QPalette::Window, QColor("#eef1f4"));
      palette.setColor(QPalette::WindowText, QColor("#1f2933"));
      palette.setColor(QPalette::Base, QColor("#ffffff"));
      palette.setColor(QPalette::AlternateBase, QColor("#f6f8fa"));
      palette.setColor(QPalette::Text, QColor("#1f2933"));
      palette.setColor(QPalette::Button, QColor("#f4f6f8"));
      palette.setColor(QPalette::ButtonText, QColor("#1f2933"));
      palette.setColor(QPalette::ToolTipBase, QColor("#ffffff"));
      palette.setColor(QPalette::ToolTipText, QColor("#1f2933"));
      palette.setColor(QPalette::Highlight, QColor("#1e88e5"));
      palette.setColor(QPalette::HighlightedText, Qt::white);
      palette.setColor(QPalette::PlaceholderText, QColor("#9aa5b1"));
      palette.setColor(QPalette::Disabled, QPalette::Text, QColor("#9aa5b1"));
      palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor("#9aa5b1"));
      palette.setColor(QPalette::Disabled, QPalette::WindowText, QColor("#9aa5b1"));
      return palette;
   }
   QPalette palette;
   palette.setColor(QPalette::Window, QColor("#22272e"));
   palette.setColor(QPalette::WindowText, QColor("#d7dce2"));
   palette.setColor(QPalette::Base, QColor("#1e2228"));
   palette.setColor(QPalette::AlternateBase, QColor("#262c33"));
   palette.setColor(QPalette::Text, QColor("#d7dce2"));
   palette.setColor(QPalette::Button, QColor("#2d333b"));
   palette.setColor(QPalette::ButtonText, QColor("#d7dce2"));
   palette.setColor(QPalette::ToolTipBase, QColor("#2d333b"));
   palette.setColor(QPalette::ToolTipText, QColor("#d7dce2"));
   palette.setColor(QPalette::Highlight, QColor("#2f81f7"));
   palette.setColor(QPalette::HighlightedText, Qt::white);
   palette.setColor(QPalette::Light, QColor("#3a424c"));
   palette.setColor(QPalette::Midlight, QColor("#323941"));
   palette.setColor(QPalette::Mid, QColor("#2a3037"));
   palette.setColor(QPalette::Dark, QColor("#15181c"));
   palette.setColor(QPalette::Shadow, QColor("#0b0d10"));
   palette.setColor(QPalette::Link, QColor("#4fa3ff"));
   palette.setColor(QPalette::PlaceholderText, QColor("#6b7785"));
   palette.setColor(QPalette::Disabled, QPalette::Text, QColor("#6b7785"));
   palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor("#6b7785"));
   palette.setColor(QPalette::Disabled, QPalette::WindowText, QColor("#6b7785"));
   return palette;
}

QColor Theme::textOn(const QColor &background)
{
   const double luminance = 0.299 * background.redF() + 0.587 * background.greenF() + 0.114 * background.blueF();
   return luminance > 0.62 ? QColor("#1f2933") : QColor(Qt::white);
}
