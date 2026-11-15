#pragma once
#include <QString>

namespace hr::ai {

// Headless speech recognition for scripts and for the CI smoke test:
//   HRPremiere --transcribe file --model small --language auto --out result.srt
// Downloads the model when needed.  Returns 0 on success, 2 on an error, 3 when no speech was found.
// `clean` = none | light | strong | auto (measure the sound and pick); `rawPrefilter` = an ffmpeg audio filter
// chain that replaces it (for experiments).
int runTranscribeCli(const QString& mediaPath, const QString& modelId, const QString& language, const QString& outPath,
                     const QString& clean = "auto", const QString& rawPrefilter = QString());

}  // namespace hr::ai
