#pragma once
#include <QColor>
#include <QFont>
#include <QString>

class QApplication;

namespace hr::theme {

// HR Premiere palette: Premiere-style dark panels with the purple of the HR logo.
inline const QColor bg0("#161616");       // gaps between panels
inline const QColor bg1("#1f1f1f");       // panel body
inline const QColor bg2("#262626");       // headers, tracks
inline const QColor bg3("#2f2f2f");       // controls
inline const QColor bg4("#3a3a3a");       // hover
inline const QColor border("#101010");
inline const QColor line("#3b3b3b");
inline const QColor text("#d6d6d6");
inline const QColor textDim("#8d8d8d");
inline const QColor accent("#9184d9");    // logo purple
inline const QColor accentHi("#b4a9f2");
inline const QColor accentDim("#5b5294");
inline const QColor videoClip("#5a64c8");
inline const QColor audioClip("#2f9a76");
inline const QColor titleClip("#b6589f");
inline const QColor stillClip("#7a9d4c");
inline const QColor danger("#e0585f");

void apply(QApplication& app);
QString styleSheet();

}  // namespace hr::theme
