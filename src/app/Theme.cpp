#include "app/Theme.h"

#include <QApplication>
#include <QGuiApplication>
#include <QPalette>
#include <QStyleFactory>
#include <QStyleHints>

namespace studio
{

void applyTheme(QApplication &app)
{
    app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));

    const bool dark = QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark;
    QPalette pal;
    if (dark)
    {
        pal.setColor(QPalette::Window, QColor(0x1F, 0x21, 0x24));
        pal.setColor(QPalette::WindowText, QColor(0xE6, 0xE6, 0xE6));
        pal.setColor(QPalette::Base, QColor(0x17, 0x18, 0x1A));
        pal.setColor(QPalette::AlternateBase, QColor(0x24, 0x26, 0x2A));
        pal.setColor(QPalette::Text, QColor(0xE6, 0xE6, 0xE6));
        pal.setColor(QPalette::Button, QColor(0x2A, 0x2C, 0x30));
        pal.setColor(QPalette::ButtonText, QColor(0xE6, 0xE6, 0xE6));
        pal.setColor(QPalette::Highlight, QColor(0x4C, 0x8D, 0xD6));
        pal.setColor(QPalette::HighlightedText, Qt::white);
        pal.setColor(QPalette::ToolTipBase, QColor(0x2A, 0x2C, 0x30));
        pal.setColor(QPalette::ToolTipText, QColor(0xE6, 0xE6, 0xE6));
        pal.setColor(QPalette::PlaceholderText, QColor(0x8A, 0x8D, 0x93));
        pal.setColor(QPalette::Disabled, QPalette::Text, QColor(0x6B, 0x6E, 0x73));
        pal.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(0x6B, 0x6E, 0x73));
    }
    else
    {
        pal.setColor(QPalette::Window, QColor(0xF5, 0xF6, 0xF8));
        pal.setColor(QPalette::WindowText, QColor(0x1F, 0x23, 0x28));
        pal.setColor(QPalette::Base, Qt::white);
        pal.setColor(QPalette::AlternateBase, QColor(0xF0, 0xF2, 0xF5));
        pal.setColor(QPalette::Text, QColor(0x1F, 0x23, 0x28));
        pal.setColor(QPalette::Button, QColor(0xFB, 0xFB, 0xFC));
        pal.setColor(QPalette::ButtonText, QColor(0x1F, 0x23, 0x28));
        pal.setColor(QPalette::Highlight, QColor(0x2F, 0x6D, 0xB5));
        pal.setColor(QPalette::HighlightedText, Qt::white);
        pal.setColor(QPalette::PlaceholderText, QColor(0x8A, 0x8F, 0x98));
    }
    app.setPalette(pal);

    app.setStyleSheet(QStringLiteral(R"(
        QLabel#pageTitle { font-size: 20pt; font-weight: 600; }
        QLabel#sectionTitle { font-size: 11pt; font-weight: 600; padding-top: 6px; }
        QLabel#viewTitle { font-weight: 600; }
        QLabel#subtle { color: palette(placeholder-text); }
        QToolBar { border: none; spacing: 4px; padding: 4px; }
        QDockWidget::title { padding: 6px; font-weight: 600; }
        QTabWidget::pane { border: none; }
        QTabBar::tab { padding: 8px 16px; }
        QStatusBar QPushButton { padding: 2px 12px; }
        QListWidget#stageBar { background: palette(window); border-right: 1px solid palette(mid);
                               padding: 8px 6px; font-size: 10.5pt; }
        QListWidget#stageBar::item { border-radius: 6px; padding: 6px 8px; color: palette(text); }
        QListWidget#stageBar::item:hover { background: palette(alternate-base); }
        QListWidget#stageBar::item:selected { background: palette(highlight);
                                              color: palette(highlighted-text); }
    )"));
}

} // namespace studio
