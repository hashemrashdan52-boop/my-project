#include "Silence.h"

#include "core/Util.h"

#include <algorithm>
#include <cmath>

namespace hr::ai {

LevelMap buildLevelMap(const Timeline& tl, const PeaksProvider& peaks) {
    LevelMap m;
    const double dur = tl.duration();
    if (dur <= 0) return m;
    const int bins = int(std::ceil(dur * 100.0)) + 1;
    m.level.assign(size_t(bins), 0.0f);
    m.covered.assign(size_t(bins), 0);

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
                m.incomplete = true;
                for (int b = b0; b <= b1; ++b) { m.covered[size_t(b)] = 1; m.level[size_t(b)] = 1.0f; }
                continue;
            }
            for (int b = b0; b <= b1; ++b) {
                const double srcT = c.srcIn + (double(b) / 100.0 - c.start) * c.speed;
                const int i0 = int(std::floor(srcT * 100.0));
                const int i1 = std::max(i0, int(std::floor((srcT + c.speed / 100.0) * 100.0)) - 1);   // fast clips cover several bins
                float v = 0;
                for (int i = std::max(0, i0); i <= i1 && i < pk->size(); ++i) v = std::max(v, float((*pk)[i]) / 255.0f * float(gain));
                m.covered[size_t(b)] = 1;
                m.level[size_t(b)] = std::max(m.level[size_t(b)], v);
            }
        }
    }
    return m;
}

static double toDb(float v) { return 20.0 * std::log10(std::max(1e-6, double(v))); }

double levelPercentileDb(const LevelMap& map, double a, double b, double percentile) {
    if (map.bins() == 0 || b <= a) return -120.0;
    const int b0 = std::max(0, int(std::floor(a * 100.0)));
    const int b1 = std::min(map.bins(), int(std::ceil(b * 100.0)));
    std::vector<float> v;
    v.reserve(size_t(std::max(0, b1 - b0)));
    for (int i = b0; i < b1; ++i)
        if (map.covered[size_t(i)]) v.push_back(map.level[size_t(i)]);
    if (v.empty()) return -120.0;
    const size_t k = std::min(v.size() - 1, size_t(std::floor(std::clamp(percentile, 0.0, 1.0) * double(v.size() - 1) + 0.5)));
    std::nth_element(v.begin(), v.begin() + std::ptrdiff_t(k), v.end());
    return toDb(v[k]);
}

AudioStats measureAudio(const LevelMap& map) {
    AudioStats s;
    if (map.bins() == 0 || map.incomplete) return s;
    std::vector<float> v;
    v.reserve(map.level.size());
    for (size_t i = 0; i < map.level.size(); ++i)
        if (map.covered[i]) v.push_back(map.level[i]);
    if (v.size() < 100) return s;      // less than a second of sound: nothing to measure
    std::sort(v.begin(), v.end());
    auto pct = [&](double p) { return toDb(v[std::min(v.size() - 1, size_t(p * double(v.size() - 1) + 0.5))]); };
    s.valid = true;
    s.noiseFloorDb = pct(0.05);       // the quietest 5 %: speech with hardly any pauses still has dips down to the noise
    s.speechDb = pct(0.90);
    s.seconds = double(v.size()) / 100.0;
    return s;
}

AudioStats measureAudio(const QVector<quint8>& peaks100) {
    LevelMap m;
    m.level.resize(size_t(peaks100.size()));
    m.covered.assign(size_t(peaks100.size()), 1);
    for (int i = 0; i < peaks100.size(); ++i) m.level[size_t(i)] = float(peaks100[i]) / 255.0f;
    return measureAudio(m);
}

QVector<TimeRange> findSilences(const LevelMap& map, double fps, const SilenceOptions& opt) {
    QVector<TimeRange> out;
    const int bins = map.bins();
    if (bins == 0) return out;
    const float thr = float(dbToGain(opt.thresholdDb));
    int runStart = -1;
    auto closeRun = [&](int endBin) {   // run covers bins [runStart, endBin)
        if (runStart < 0) return;
        double s = double(runStart) / 100.0, e = double(endBin) / 100.0;
        if (e - s >= opt.minSilence) {
            s += opt.padding;
            e -= opt.padding;
            // snap inwards to whole frames so the cut never touches speech
            s = std::ceil(s * fps - 1e-6) / fps;
            e = std::floor(e * fps + 1e-6) / fps;
            if (e - s >= 2.0 / fps) out.push_back({s, e});
        }
        runStart = -1;
    };
    for (int b = 0; b < bins; ++b) {
        const bool silent = map.covered[size_t(b)] && map.level[size_t(b)] < thr;
        if (silent) { if (runStart < 0) runStart = b; }
        else closeRun(b);
    }
    closeRun(bins);
    return out;
}

QVector<TimeRange> findSilences(const Timeline& tl, const PeaksProvider& peaks, const SilenceOptions& opt, bool* incomplete) {
    if (incomplete) *incomplete = false;
    if (tl.duration() <= 0) return {};
    const LevelMap map = buildLevelMap(tl, peaks);
    if (incomplete) *incomplete = map.incomplete;
    return findSilences(map, tl.fps, opt);
}

}  // namespace hr::ai
