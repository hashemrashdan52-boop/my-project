#pragma once
#include <QColor>
#include <QIcon>
#include <QPixmap>

namespace hr::icons {

enum Name {
    Play, Pause, StepBack, StepForward, ToStart, ToEnd, MarkIn, MarkOut, Insert, Overwrite, Camera,
    Selection, TrackForward, Ripple, Razor, Hand, Zoom, Type,
    Magnet, Link, Marker, Wrench, Eye, EyeOff, Lock, Unlock, Speaker, SpeakerOff,
    Folder, ListView, IconView, Search, Trash, Plus, Film, Music, ImageFile, Title, Fx, Import, Check, Mic
};

// Vector icon drawn in code, tinted with `color`; `active` is used for the checked/on state.
QIcon make(Name n, const QColor& color, const QColor& active = QColor());
QIcon make(Name n);                       // default theme colours
QPixmap pixmap(Name n, const QColor& color, int size);
QPixmap logo(int size);                   // the HR logo (rendered from the bundled SVG)

}  // namespace hr::icons
