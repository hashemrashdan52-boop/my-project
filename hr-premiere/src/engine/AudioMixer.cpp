#include "AudioMixer.h"

#include "Renderer.h"  // fadeFactor
#include "core/Util.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace hr {

void AudioMixer::render(const Timeline& tl, qint64 startSample, int frames, float* out) {
    std::memset(out, 0, sizeof(float) * size_t(frames) * 2);
    bool anySolo = false;
    for (const Track& tr : tl.audio) anySolo = anySolo || tr.solo;

    const qint64 endSample = startSample + frames;
    for (int ti = 0; ti < tl.audio.size(); ++ti) {
        const Track& tr = tl.audio[ti];
        if (!tr.enabled || (anySolo && !tr.solo)) continue;
        for (const Clip& c : tr.clips) {
            if (!c.enabled) continue;
            const qint64 cs = std::llround(c.start * kRate);
            const qint64 ce = std::llround(c.end() * kRate);
            if (ce <= startSample || cs >= endSample) continue;
            const qint64 s0 = std::max(startSample, cs);
            const qint64 s1 = std::min(endSample, ce);
            const int n = int(s1 - s0);
            if (n <= 0) continue;
            AudioDecoder* dec = m_pool->audio(c.path + "#a" + QString::number(ti), c.path);
            if (!dec) continue;

            const double srcT = c.srcIn + double(s0 - cs) / kRate;
            m_tmp.resize(size_t(n) * 2);
            dec->read(srcT, m_tmp.data(), n);

            const EffectInstance* vol = c.effect("volume");
            const EffectInstance* bal = nullptr;
            for (const EffectInstance& e : c.effects) if (e.enabled && e.type == "balance") bal = &e;
            const double gain = vol ? dbToGain(vol->num("level")) : 1.0;
            const double fi = vol ? vol->num("fade_in") : 0, fo = vol ? vol->num("fade_out") : 0;
            const double pan = bal ? bal->num("pan") / 100.0 : 0.0;
            const float gl = float(gain * (pan > 0 ? 1.0 - pan : 1.0));
            const float gr = float(gain * (pan < 0 ? 1.0 + pan : 1.0));
            const bool fading = fi > 0 || fo > 0;
            float* dst = out + size_t(s0 - startSample) * 2;
            for (int i = 0; i < n; ++i) {
                float env = 1.0f;
                if (fading) {
                    const double local = double(s0 - cs + i) / kRate;
                    env = float(fadeFactor(local, c.duration, fi, fo));
                }
                dst[2 * i] += m_tmp[2 * i] * gl * env;
                dst[2 * i + 1] += m_tmp[2 * i + 1] * gr * env;
            }
        }
    }
    float pl = 0, pr = 0;
    for (int i = 0; i < frames; ++i) {
        float& l = out[2 * i];
        float& r = out[2 * i + 1];
        l = std::max(-1.0f, std::min(1.0f, l));
        r = std::max(-1.0f, std::min(1.0f, r));
        pl = std::max(pl, std::fabs(l));
        pr = std::max(pr, std::fabs(r));
    }
    m_peakL = pl;
    m_peakR = pr;
}

}  // namespace hr
