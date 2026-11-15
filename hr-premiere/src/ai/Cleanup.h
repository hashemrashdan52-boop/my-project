#pragma once
#include "Silence.h"

#include <QString>

namespace hr::ai {

// What is done to the sound before the speech model listens to it.  Speech models cope with noise surprisingly
// well, but very quiet or hissy recordings still lose words, so the sound is levelled (and, for really noisy
// recordings, denoised) first.  The original file is never changed: this only happens inside the ffmpeg
// process that feeds the model.
enum class Cleanup { None, Light, Strong };

QString cleanupName(Cleanup c);                          // "none" / "light" / "strong"
Cleanup cleanupFromName(const QString& name, Cleanup fallback);
// The ffmpeg audio filter chain for a level ("" for None).
QString cleanupFilter(Cleanup c);
// Which level suits a recording, from its measured noise floor and speech level.
Cleanup chooseCleanup(const AudioStats& stats);

}  // namespace hr::ai
