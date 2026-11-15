#pragma once
#include <QString>

namespace hr::ai {

// Headless speech recognition for scripts and for the CI smoke test:
//   HRPremiere --transcribe file --model small --language auto --out result.srt
// Downloads the model when needed.  Returns 0 on success, 2 on an error, 3 when no speech was found.
int runTranscribeCli(const QString& mediaPath, const QString& modelId, const QString& language, const QString& outPath);

}  // namespace hr::ai
