#include "Theme.h"

#include <QApplication>
#include <QPalette>
#include <QSettings>
#include <QStyle>
#include <QStyleFactory>
#include <QWidget>

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
QLabel#accentText { color: %11; }
QLabel#cardTitle { color: %11; font-weight: 700; font-size: 13px; background: transparent; }
#tlHeader { background: %2; }
QLabel#timecode { color: %11; font-size: 18px; font-weight: 600; font-family: 'DejaVu Sans Mono', Consolas, monospace; }
QLabel#monitorTime { color: %11; font-size: 16px; font-weight: 600; font-family: 'DejaVu Sans Mono', Consolas, monospace; }
QLabel#monitorDuration { color: %8; font-size: 14px; font-family: 'DejaVu Sans Mono', Consolas, monospace; }
QLabel#bubbleUser { background: %12; color: white; border-radius: 10px; padding: 7px 10px; }
QLabel#bubbleBot { background: %3; color: %7; border: 1px solid %9; border-radius: 10px; padding: 7px 10px; }
)")
        .arg(bg1.name(), bg2.name(), bg3.name(), bg3.name(), bg4.name(), border.name(), text.name(), textDim.name())
        .arg(line.name(), accent.name(), accentHi.name(), accentDim.name());
}

// ------------------------------------------------------------------------------------------------ the themes
static Theme make(const char* id, const char* name, bool dark, const char* b0, const char* b1, const char* b2, const char* b3,
                  const char* b4, const char* brd, const char* ln, const char* tx, const char* txd, const char* ac,
                  const char* achi, const char* acdim) {
    Theme t;
    t.id = id; t.name = name; t.dark = dark;
    t.bg0 = b0; t.bg1 = b1; t.bg2 = b2; t.bg3 = b3; t.bg4 = b4; t.border = brd; t.line = ln;
    t.text = tx; t.textDim = txd; t.accent = ac; t.accentHi = achi; t.accentDim = acdim;
    return t;
}

const QVector<Theme>& all() {
    static const QVector<Theme> themes = {
        //   id          name                 dark  bg0       bg1       bg2       bg3       bg4       border    line      text      textDim   accent    accentHi  accentDim
        make("hr",       "HR Purple",         true, "#161616", "#1f1f1f", "#262626", "#2f2f2f", "#3a3a3a", "#101010", "#3b3b3b", "#d6d6d6", "#8d8d8d", "#9184d9", "#b4a9f2", "#5b5294"),
        make("classic",  "Classic",           true, "#141414", "#232323", "#2b2b2b", "#343434", "#414141", "#0d0d0d", "#3f3f3f", "#d9d9d9", "#939393", "#2d8ceb", "#6bb0f5", "#245f9c"),
        make("midnight", "Midnight Blue",     true, "#0b1020", "#111a30", "#17223d", "#1f2c4d", "#2a3a63", "#070b16", "#2c3b63", "#dfe7ff", "#8e9bc0", "#4f8cff", "#8ab4ff", "#2f5bb5"),
        make("ocean",    "Ocean",             true, "#0a1719", "#102226", "#162d32", "#1d3a40", "#274b52", "#06100f", "#28474d", "#d9f3f6", "#82a9ae", "#22c3d6", "#6fe0ee", "#16808d"),
        make("emerald",  "Emerald",           true, "#0d1714", "#13211c", "#1a2c25", "#223a31", "#2c4a3f", "#08100d", "#2f4d42", "#dcf2e8", "#86a999", "#2ec27e", "#6fe3ad", "#1d7d52"),
        make("sunset",   "Sunset",            true, "#1a1210", "#241917", "#2e211e", "#3a2a26", "#4a352f", "#120c0b", "#4a3731", "#f3e3dc", "#b79a8e", "#ff7a45", "#ffa179", "#b2522b"),
        make("rose",     "Rose",              true, "#1b1117", "#261820", "#311f29", "#3e2833", "#4f3340", "#120b10", "#52374a", "#f6e3ec", "#b898a8", "#ff5c9a", "#ff8fba", "#b23d6c"),
        make("light",    "Light",             false, "#cfd3db", "#eceef2", "#e0e4ea", "#d4d9e2", "#c5ccd9", "#aeb5c4", "#bcc3d0", "#1c2030", "#667085", "#6b5bd6", "#4a3fb5", "#7a6ee0"),
    };
    return themes;
}

const Theme* find(const QString& id) {
    for (const Theme& t : all())
        if (t.id == id) return &t;
    return nullptr;
}

static QString g_id = kDefaultId;
static QColor g_accentOverride;

QString currentId() { return g_id; }
QColor accentOverride() { return g_accentOverride; }

void use(const QString& id, const QColor& accentOverrideColor) {
    const Theme* t = find(id);
    if (!t) t = find(kDefaultId);
    g_id = t->id;
    g_accentOverride = accentOverrideColor.isValid() ? accentOverrideColor : QColor();
    dark = t->dark;
    bg0 = t->bg0; bg1 = t->bg1; bg2 = t->bg2; bg3 = t->bg3; bg4 = t->bg4;
    border = t->border; line = t->line; text = t->text; textDim = t->textDim;
    accent = t->accent; accentHi = t->accentHi; accentDim = t->accentDim;
    if (g_accentOverride.isValid()) {
        accent = g_accentOverride;
        accentHi = dark ? g_accentOverride.lighter(145) : g_accentOverride.darker(125);
        accentDim = dark ? g_accentOverride.darker(165) : g_accentOverride.lighter(115);
    }
}

void loadSaved() {
    QSettings s;
    const QString acc = s.value("ui/accent").toString();
    use(s.value("ui/theme", kDefaultId).toString(), acc.isEmpty() ? QColor() : QColor(acc));
}

void save() {
    QSettings s;
    s.setValue("ui/theme", g_id);
    if (g_accentOverride.isValid()) s.setValue("ui/accent", g_accentOverride.name());
    else s.remove("ui/accent");
}

Notifier* notifier() {
    static Notifier n;
    return &n;
}

void refresh(QApplication& app) {
    apply(app);
    emit notifier()->changed();
    for (QWidget* w : app.allWidgets()) {
        w->style()->unpolish(w);
        w->style()->polish(w);
        w->update();
    }
}

void refresh() {
    if (auto* app = qobject_cast<QApplication*>(QCoreApplication::instance())) refresh(*app);
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
    p.setColor(QPalette::BrightText, dark ? QColor(Qt::white) : QColor(Qt::black));
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
