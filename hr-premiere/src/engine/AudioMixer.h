#pragma once
#include "DecoderPool.h"
#include "core/Model.h"

namespace hr {

// Mixes all audio tracks of a timeline into 48 kHz interleaved stereo float.
class AudioMixer {
public:
    static constexpr int kRate = AudioDecoder::kRate;

    explicit AudioMixer(DecoderPool* pool) : m_pool(pool) {}

    // Renders `frames` frames starting at timeline sample `startSample` into out (2*frames floats).
    void render(const Timeline& tl, qint64 startSample, int frames, float* out);

    float peakL() const { return m_peakL; }
    float peakR() const { return m_peakR; }

private:
    DecoderPool* m_pool;
    std::vector<float> m_tmp;
    float m_peakL = 0, m_peakR = 0;
};

}  // namespace hr
