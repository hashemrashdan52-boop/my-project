#pragma once
#include "core/Model.h"

#include <QVector>
#include <functional>

namespace hr::ai {

struct SilenceOptions {
    double thresholdDb = -40.0;  // anything quieter than this counts as silence
    double minSilence = 0.6;     // only silences at least this long are removed (seconds)
    double padding = 0.12;       // keep this much of the silence at both ends so speech is not clipped
};

// Returns the peaks of a media file: one byte (0..255) per 10 ms; nullptr while they are not ready.
using PeaksProvider = std::function<const QVector<quint8>*(const QString& mediaId)>;

// Finds the silent stretches of a sequence, in timeline seconds.  Only places that are covered by an audio
// clip are considered, so stills and titles without sound are never cut.  `incomplete` is set when the
// waveform of some clip was not ready yet (those clips are treated as loud).
QVector<TimeRange> findSilences(const Timeline& tl, const PeaksProvider& peaks, const SilenceOptions& opt, bool* incomplete = nullptr);

}  // namespace hr::ai
