#include "Theme.h"

#include <QApplication>
#include <QPalette>
#include <QStyleFactory>

namespace hr::theme {

QString styleSheet() {
    return QString(R"(
* { font-size: 12px; }
QMainWindow, QDialog { background: %1; }
QWidget { color: %7; }
QToolTip { background: %3; color: %7; border: 1px solid %9; padding: 4px; }

QMenuBar { background: %2; color: %7; padding: 1px 4px; }
QMenuBar::item { padding: 5px 10px; background: transparent; border-radius: 3px; }
QMenuBar::item:selected { background: %5; }
QMenu { background: %3; border: 1px solid %9; padding: 4px; }
QMenu::item { padding: 6px 28px 6px 24px; border-radius: 3px; }
QMenu::item:selected { background: %10; color: white; }
QMenu::item:disabled { color: %8; }
QMenu::separator { height: 1px; background: %9; margin: 4px 6px; }

QDockWidget { color: %7; titlebar-close-icon: none; titlebar-normal-icon: none; }
QDockWidget::title { background: %2; padding: 6px 10px; border-bottom: 1px solid %6; text-align: left; font-weight: 600; }
QMainWindow::separator { background: %1; width: 5px; height: 5px; }
QMainWindow::separator:hover { background: %10; }

QTabBar { background: %2; qproperty-drawBase: 0; }
QTabBar::tab { background: %2; color: %8; padding: 6px 14px; border: none; border-bottom: 2px solid transparent; }
QTabBar::tab:selected { color: %7; border-bottom: 2px solid %10; }
QTabBar::tab:hover { color: white; }

QPushButton { background: %4; border: 1px solid %9; border-radius: 4px; padding: 6px 14px; }
QPushButton:hover { background: %5; }
QPushButton:pressed { background: %2; }
QPushButton:disabled { color: %8; background: %3; }
QPushButton#primary { background: %10; border: 1px solid %10; color: white; font-weight: 600; }
QPushButton#primary:hover { background: %11; }
QToolButton { background: transparent; border: 1px solid transparent; border-radius: 4px; padding: 3px; }
QToolButton:hover { background: %5; }
QToolButton:checked { background: %12; border: 1px solid %10; }

QLineEdit, QPlainTextEdit, QSpinBox, QDoubleSpinBox, QComboBox, QFontComboBox {
    background: %2; border: 1px solid %9; border-radius: 3px; padding: 3px 6px; selection-background-color: %10; }
QLineEdit:focus, QPlainTextEdit:focus, QSpinBox:focus, QDoubleSpinBox:focus, QComboBox:focus { border: 1px solid %10; }
QComboBox::drop-down { border: none; width: 18px; }
QComboBox QAbstractItemView { background: %3; border: 1px solid %9; selection-background-color: %10; outline: 0; }
QSpinBox::up-button, QSpinBox::down-button, QDoubleSpinBox::up-button, QDoubleSpinBox::down-button { width: 0; }

QListWidget, QTreeWidget, QTableWidget { background: %1; border: none; outline: 0; }
QListWidget::item { color: %7; border-radius: 3px; padding: 2px; }
QListWidget::item:selected, QTreeWidget::item:selected { background: %12; color: white; }
QListWidget::item:hover, QTreeWidget::item:hover { background: %5; }
QHeaderView::section { background: %2; color: %8; border: none; padding: 4px 8px; }

QScrollBar:vertical { background: %1; width: 12px; margin: 0; }
QScrollBar:horizontal { background: %1; height: 12px; margin: 0; }
QScrollBar::handle { background: %4; border-radius: 4px; min-height: 30px; min-width: 30px; margin: 2px; }
QScrollBar::handle:hover { background: %5; }
QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }

QSlider::groove:horizontal { height: 4px; background: %4; border-radius: 2px; }
QSlider::sub-page:horizontal { background: %10; border-radius: 2px; }
QSlider::handle:horizontal { background: %7; width: 12px; height: 12px; margin: -4px 0; border-radius: 6px; }
QSlider::handle:horizontal:hover { background: white; }

QProgressBar { background: %2; border: 1px solid %9; border-radius: 4px; text-align: center; height: 18px; }
QProgressBar::chunk { background: %10; border-radius: 3px; }

QCheckBox { spacing: 6px; }
QCheckBox::indicator { width: 14px; height: 14px; border: 1px solid %8; border-radius: 3px; background: %2; }
QCheckBox::indicator:checked { background: %10; border: 1px solid %10; }

QGroupBox { border: 1px solid %9; border-radius: 4px; margin-top: 10px; padding-top: 8px; }
QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 4px; color: %8; }
QSplitter::handle { background: %1; }
QStatusBar { background: %2; color: %8; }
QStatusBar::item { border: none; }
QScrollArea { border: none; background: %1; }
QFrame#card { background: %2; border: 1px solid %9; border-radius: 4px; }
QLabel#dim { color: %8; }
)")
        .arg(bg1.name(), bg2.name(), bg3.name(), bg3.name(), bg4.name(), border.name(), text.name(), textDim.name())
        .arg(line.name(), accent.name(), accentHi.name(), accentDim.name());
}

void apply(QApplication& app) {
    app.setStyle(QStyleFactory::create("Fusion"));
    QPalette p;
    p.setColor(QPalette::Window, bg1);
    p.setColor(QPalette::WindowText, text);
    p.setColor(QPalette::Base, bg2);
    p.setColor(QPalette::AlternateBase, bg1);
    p.setColor(QPalette::ToolTipBase, bg3);
    p.setColor(QPalette::ToolTipText, text);
    p.setColor(QPalette::Text, text);
    p.setColor(QPalette::Button, bg3);
    p.setColor(QPalette::ButtonText, text);
    p.setColor(QPalette::BrightText, Qt::white);
    p.setColor(QPalette::Highlight, accent);
    p.setColor(QPalette::HighlightedText, Qt::white);
    p.setColor(QPalette::PlaceholderText, textDim);
    p.setColor(QPalette::Disabled, QPalette::Text, textDim);
    p.setColor(QPalette::Disabled, QPalette::ButtonText, textDim);
    p.setColor(QPalette::Disabled, QPalette::WindowText, textDim);
    app.setPalette(p);
    app.setStyleSheet(styleSheet());
}

}  // namespace hr::theme
