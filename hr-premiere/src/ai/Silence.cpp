#include "Silence.h"

#include "core/Util.h"

#include <algorithm>
#include <cmath>

namespace hr::ai {

QVector<TimeRange> findSilences(const Timeline& tl, const PeaksProvider& peaks, const SilenceOptions& opt, bool* incomplete) {
    if (incomplete) *incomplete = false;
    const double dur = tl.duration();
    if (dur <= 0) return {};
    const int bins = int(std::ceil(dur * 100.0)) + 1;
    std::vector<float> level(size_t(bins), 0.0f);
    std::vector<char> covered(size_t(bins), 0);

    bool anySolo = false;
    for (const Track& t : tl.audio) anySolo = anySolo || t.solo;
    for (const Track& tr : tl.audio) {
        if (!tr.enabled || (anySolo && !tr.solo)) continue;
        for (const Clip& c : tr.clips) {
            if (!c.enabled) continue;
            const QVector<quint8>* pk = c.mediaId.isEmpty() ? nullptr : peaks(c.mediaId);
            const EffectInstance* vol = c.effect("volume");
            const double gain = vol && vol->enabled ? dbToGain(vol->num("level")) : 1.0;
            const int b0 = std::max(0, int(std::floor(c.start * 100.0)));
            const int b1 = std::min(bins - 1, int(std::ceil(c.end() * 100.0)));
            if (!pk || pk->isEmpty()) {
                if (incomplete) *incomplete = true;
                for (int b = b0; b <= b1; ++b) { covered[size_t(b)] = 1; level[size_t(b)] = 1.0f; }
                continue;
            }
            for (int b = b0; b <= b1; ++b) {
                const double srcT = c.srcIn + (double(b) / 100.0 - c.start);
                const int idx = int(std::floor(srcT * 100.0));
                float v = 0;
                if (idx >= 0 && idx < pk->size()) v = float((*pk)[idx]) / 255.0f * float(gain);
                covered[size_t(b)] = 1;
                level[size_t(b)] = std::max(level[size_t(b)], v);
            }
        }
    }

    const float thr = float(dbToGain(opt.thresholdDb));
    QVector<TimeRange> out;
    int runStart = -1;
    auto closeRun = [&](int endBin) {   // run covers bins [runStart, endBin)
        if (runStart < 0) return;
        double s = double(runStart) / 100.0, e = double(endBin) / 100.0;
        if (e - s >= opt.minSilence) {
            s += opt.padding;
            e -= opt.padding;
            // snap inwards to whole frames so the cut never touches speech
            s = std::ceil(s * tl.fps - 1e-6) / tl.fps;
            e = std::floor(e * tl.fps + 1e-6) / tl.fps;
            if (e - s >= 2.0 / tl.fps) out.push_back({s, e});
        }
        runStart = -1;
    };
    for (int b = 0; b < bins; ++b) {
        const bool silent = covered[size_t(b)] && level[size_t(b)] < thr;
        if (silent) { if (runStart < 0) runStart = b; }
        else closeRun(b);
    }
    closeRun(bins);
    return out;
}

}  // namespace hr::ai
