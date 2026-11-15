#pragma once
#include "core/Model.h"

#include <QVector>
#include <functional>
#include <vector>

namespace hr::ai {

struct SilenceOptions {
    double thresholdDb = -40.0;  // anything quieter than this counts as silence
    double minSilence = 0.6;     // only silences at least this long are removed (seconds)
    double padding = 0.12;       // keep this much of the silence at both ends so speech is not clipped
};

// Returns the peaks of a media file: one byte (0..255) per 10 ms; nullptr while they are not ready.
using PeaksProvider = std::function<const QVector<quint8>*(const QString& mediaId)>;

// How loud the sequence is, one value per 10 ms of timeline.
struct LevelMap {
    std::vector<float> level;    // 0..1 peak amplitude (the clip's Volume applied)
    std::vector<char> covered;   // 1 where an audio clip plays
    bool incomplete = false;     // the waveform of some clip was not ready (those clips count as loud)
    int bins() const { return int(level.size()); }
};
LevelMap buildLevelMap(const Timeline& tl, const PeaksProvider& peaks);

// Loudness (dBFS, peak based) of the sound in [a, b) at the given percentile (0..1) of the covered bins;
// -120 when nothing plays there.
double levelPercentileDb(const LevelMap& map, double a, double b, double percentile);

// What the sound of the whole sequence is like.
struct AudioStats {
    bool valid = false;
    double noiseFloorDb = -60;   // the quiet 15 % of the time
    double speechDb = -20;       // the loud 10 % of the time
    double dynamicDb() const { return speechDb - noiseFloorDb; }
    double seconds = 0;          // how much sound was measured
};
AudioStats measureAudio(const LevelMap& map);
// The same for the waveform of one media file (one byte per 10 ms).
AudioStats measureAudio(const QVector<quint8>& peaks100);

// Finds the silent stretches of a sequence, in timeline seconds.  Only places that are covered by an audio
// clip are considered, so stills and titles without sound are never cut.  `incomplete` is set when the
// waveform of some clip was not ready yet (those clips are treated as loud).
QVector<TimeRange> findSilences(const Timeline& tl, const PeaksProvider& peaks, const SilenceOptions& opt, bool* incomplete = nullptr);
// The same on a level map that was already built.
QVector<TimeRange> findSilences(const LevelMap& map, double fps, const SilenceOptions& opt);

}  // namespace hr::ai
