#include "Scenes.h"

#include "media/VideoDecoder.h"

#include <QImage>
#include <algorithm>
#include <cmath>

namespace hr::ai {

namespace {

struct Sig {
    // 64 x 36 luma thumbnail + 16-bin histogram of it
    std::vector<uint8_t> luma;
    float hist[16] = {0};
    bool valid = false;
};

Sig signatureOf(const QImage& img) {
    Sig s;
    if (img.isNull()) return s;
    const QImage small = img.scaled(64, 36, Qt::IgnoreAspectRatio, Qt::FastTransformation).convertToFormat(QImage::Format_RGB32);
    s.luma.resize(64 * 36);
    for (int y = 0; y < 36; ++y) {
        const QRgb* line = reinterpret_cast<const QRgb*>(small.constScanLine(y));
        for (int x = 0; x < 64; ++x) {
            const QRgb c = line[x];
            const int l = (qRed(c) * 77 + qGreen(c) * 151 + qBlue(c) * 28) >> 8;
            s.luma[size_t(y * 64 + x)] = uint8_t(l);
            s.hist[l >> 4] += 1.0f / (64 * 36);
        }
    }
    s.valid = true;
    return s;
}

// 0 = identical, 1 = completely different
double difference(const Sig& a, const Sig& b) {
    if (!a.valid || !b.valid) return 0;
    double pix = 0;
    for (size_t i = 0; i < a.luma.size(); ++i) pix += std::abs(int(a.luma[i]) - int(b.luma[i]));
    pix /= double(a.luma.size()) * 255.0;
    double hist = 0;
    for (int i = 0; i < 16; ++i) hist += std::abs(a.hist[i] - b.hist[i]);
    hist *= 0.5;
    return 0.5 * pix + 0.5 * hist;
}

}  // namespace

QVector<double> detectScenes(const QString& path, const SceneOptions& opt, const std::function<void(double)>& progress,
                             const std::atomic<bool>* cancel) {
    QVector<double> cuts;
    VideoDecoder dec;
    if (!dec.open(path)) return cuts;
    const double dur = dec.duration();
    const double fps = std::max(1.0, dec.fps());
    if (dur <= 0.3) return cuts;

    // sensitivity 0 -> only hard cuts, 1 -> also small changes
    const double threshold = 0.40 - 0.30 * std::max(0.0, std::min(1.0, opt.sensitivity));
    const double step = 0.2;
    const QSize thumb(96, 54);

    auto sigAt = [&](double t) { return signatureOf(dec.frameAt(std::max(0.0, std::min(t, dur - 0.5 / fps)), thumb)); };

    Sig prev = sigAt(0);
    double prevT = 0;
    std::vector<double> recent;   // the last few differences: a cut must stand out from the local motion
    double lastCut = -1e9;
    for (double t = step; t < dur; t += step) {
        if (cancel && cancel->load()) break;
        Sig cur = sigAt(t);
        const double d = difference(prev, cur);
        double avg = 0;
        for (double v : recent) avg += v;
        avg = recent.empty() ? 0 : avg / double(recent.size());
        if (d > threshold && d > 2.2 * avg) {
            // refine inside (prevT, t] down to one frame
            double lo = prevT, hi = t;
            const Sig base = prev;
            while (hi - lo > 1.0 / fps + 1e-6) {
                const double mid = 0.5 * (lo + hi);
                if (difference(base, sigAt(mid)) > d * 0.5) hi = mid; else lo = mid;
            }
            const double cutAt = std::round(hi * fps) / fps;
            if (cutAt - lastCut >= opt.minSceneSeconds && cutAt > opt.minSceneSeconds * 0.5) {
                cuts.push_back(cutAt);
                lastCut = cutAt;
            }
            recent.clear();
        } else {
            recent.push_back(d);
            if (recent.size() > 5) recent.erase(recent.begin());
        }
        prev = cur;
        prevT = t;
        if (progress) progress(t / dur);
    }
    if (progress) progress(1.0);
    return cuts;
}

}  // namespace hr::ai
