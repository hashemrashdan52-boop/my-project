#pragma once
#include <QColor>
#include <QFont>
#include <QObject>
#include <QString>
#include <QVector>

class QApplication;

namespace hr::theme {

// The current palette.  These are plain variables so a theme can be switched while the program runs:
// custom painted widgets read them every time they paint, and the style sheet is rebuilt from them.
// Default values = the HR theme (Premiere-style dark panels with the purple of the HR logo).
inline QColor bg0("#161616");       // gaps between panels
inline QColor bg1("#1f1f1f");       // panel body
inline QColor bg2("#262626");       // headers, tracks
inline QColor bg3("#2f2f2f");       // controls
inline QColor bg4("#3a3a3a");       // hover
inline QColor border("#101010");
inline QColor line("#3b3b3b");
inline QColor text("#d6d6d6");
inline QColor textDim("#8d8d8d");
inline QColor accent("#9184d9");    // logo purple
inline QColor accentHi("#b4a9f2");
inline QColor accentDim("#5b5294");
inline QColor videoClip("#5a64c8");
inline QColor audioClip("#2f9a76");
inline QColor titleClip("#b6589f");
inline QColor stillClip("#7a9d4c");
inline QColor danger("#e0585f");
inline bool dark = true;            // false for the light theme

// A named palette the user can pick in Preferences.
struct Theme {
    QString id;
    QString name;                   // English name (translated with TR when shown)
    bool dark = true;
    QColor bg0, bg1, bg2, bg3, bg4, border, line, text, textDim, accent, accentHi, accentDim;
};
const QVector<Theme>& all();
const Theme* find(const QString& id);
inline const char* kDefaultId = "hr";
inline const char* kClassicId = "classic";     // the traditional dark grey look with a blue accent

QString currentId();
// Makes `id` the current palette (an unknown id means the default).  With a valid `accentOverride` the accent
// colour (and its light / dark variants) is replaced.  Does not touch any widget: call refresh() afterwards.
void use(const QString& id, const QColor& accentOverride = QColor());
QColor accentOverride();                        // invalid when the theme's own accent is used

// What the user chose last time (Preferences stores it); applies it with use().
void loadSaved();
void save();

QString styleSheet();
void apply(QApplication& app);                  // Fusion style + palette + style sheet of the current colours

// Tells every widget that the colours changed so it repaints (and icons are drawn again).
class Notifier : public QObject {
    Q_OBJECT
signals:
    void changed();
};
Notifier* notifier();
// apply() + repaint everything: used when the user picks another theme while the program is running.
void refresh(QApplication& app);
void refresh();                 // the same for the running application

}  // namespace hr::theme
