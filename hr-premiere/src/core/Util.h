#pragma once
#include <QString>
#include <cmath>

namespace hr {

// "HH:MM:SS:FF" (non drop-frame, nominal fps).
QString formatTimecode(double seconds, double fps);
// Parses "HH:MM:SS:FF", "MM:SS:FF", "SS:FF", or plain frames / seconds ("90", "1.5s").
double parseTimecode(const QString& text, double fps, bool* ok = nullptr);
// "1:23" / "1:02:03" – used for durations in lists.
QString formatDuration(double seconds);

inline double snapToFrame(double t, double fps) { return std::round(t * fps) / fps; }
inline double floorToFrame(double t, double fps) { return std::floor(t * fps + 1e-6) / fps; }
inline double clampd(double v, double lo, double hi) { return v < lo ? lo : (v > hi ? hi : v); }

double dbToGain(double db);

}  // namespace hr
