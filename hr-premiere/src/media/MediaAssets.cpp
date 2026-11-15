#include "MediaAssets.h"

#include "AudioDecoder.h"
#include "FFmpegUtil.h"
#include "VideoDecoder.h"

#include <QImageReader>
#include <QRunnable>
#include <algorithm>
#include <cmath>

namespace hr {

namespace {
class Job : public QRunnable {
public:
    explicit Job(std::function<void()> fn) : m_fn(std::move(fn)) { setAutoDelete(true); }
    void run() override { m_fn(); }
private:
    std::function<void()> m_fn;
};
}  // namespace

MediaAssets::MediaAssets(QObject* parent) : QObject(parent) { m_pool.setMaxThreadCount(2); }

MediaAssets::~MediaAssets() {
    m_pool.clear();
    m_pool.waitForDone();
}

void MediaAssets::request(const MediaItem& item) {
    {
        QMutexLocker l(&m_mutex);
        if (m_started.contains(item.id) || item.offline) return;
        m_started.insert(item.id);
    }
    if (item.hasVideo || item.kind == MediaKind::Image) m_pool.start(new Job([this, item] { runThumbs(item); }));
    if (item.hasAudio) m_pool.start(new Job([this, item] { runWaveform(item); }));
}

MediaAsset MediaAssets::get(const QString& id) const {
    QMutexLocker l(&m_mutex);
    return m_assets.value(id);
}

void MediaAssets::forget(const QString& id) {
    QMutexLocker l(&m_mutex);
    m_assets.remove(id);
    m_started.remove(id);
}

void MediaAssets::runThumbs(MediaItem item) {
    MediaAsset a;
    if (item.kind == MediaKind::Image) {
        QImage img(item.path);
        if (!img.isNull()) {
            a.poster = img.scaledToWidth(320, Qt::SmoothTransformation);
            a.strip = {img.scaledToHeight(72, Qt::SmoothTransformation)};
            a.stripStep = item.duration;
        }
    } else {
        VideoDecoder dec;
        if (dec.open(item.path)) {
            const double dur = std::max(0.1, item.duration);
            const double step = std::max(1.0, dur / 48.0);
            const int count = std::max(1, int(std::floor(dur / step)));
            for (int i = 0; i < count; ++i) {
                const double t = std::min(dur - 0.05, (i + 0.5) * step);
                QImage f = dec.uprightFrameAt(std::max(0.0, t), QSize(256, 144));
                if (!f.isNull()) a.strip.push_back(f.scaledToHeight(72, Qt::SmoothTransformation));
                else if (!a.strip.isEmpty()) a.strip.push_back(a.strip.last());
            }
            a.stripStep = step;
            QImage p = dec.uprightFrameAt(std::min(dur * 0.1, dur - 0.05), QSize(480, 270));
            if (!p.isNull()) a.poster = p.scaledToWidth(std::min(320, p.width()), Qt::SmoothTransformation);
            else if (!a.strip.isEmpty()) a.poster = a.strip.first();
        }
    }
    a.thumbsReady = true;
    {
        QMutexLocker l(&m_mutex);
        MediaAsset& dst = m_assets[item.id];
        dst.poster = a.poster;
        dst.strip = a.strip;
        dst.stripStep = a.stripStep;
        dst.thumbsReady = true;
    }
    QMetaObject::invokeMethod(this, [this, id = item.id] { emit updated(id); }, Qt::QueuedConnection);
}

void MediaAssets::runWaveform(MediaItem item) {
    AudioDecoder dec;
    QVector<quint8> p100;
    if (dec.open(item.path)) {
        const int block = AudioDecoder::kRate / 100;  // 480 samples = 10 ms
        const int chunkBlocks = 100;                   // 1 s per read
        std::vector<float> buf(size_t(block) * chunkBlocks * 2);
        const double total = std::max(0.0, item.duration);
        const qint64 blocks = qint64(std::ceil(total * 100.0));
        p100.reserve(int(blocks));
        double t = 0;
        while (qint64(p100.size()) < blocks) {
            dec.read(t, buf.data(), block * chunkBlocks);
            for (int b = 0; b < chunkBlocks && qint64(p100.size()) < blocks; ++b) {
                float m = 0;
                const float* s = buf.data() + size_t(b) * block * 2;
                for (int i = 0; i < block * 2; ++i) m = std::max(m, std::fabs(s[i]));
                p100.push_back(quint8(std::min(255.0f, m * 255.0f)));
            }
            t += chunkBlocks / 100.0;
        }
    }
    auto pool = [](const QVector<quint8>& src, int factor) {
        QVector<quint8> out;
        out.reserve(src.size() / factor + 1);
        for (int i = 0; i < src.size(); i += factor) {
            quint8 m = 0;
            for (int j = i; j < std::min<int>(src.size(), i + factor); ++j) m = std::max(m, src[j]);
            out.push_back(m);
        }
        return out;
    };
    const QVector<quint8> p10 = pool(p100, 10);
    const QVector<quint8> p1 = pool(p10, 10);
    {
        QMutexLocker l(&m_mutex);
        MediaAsset& dst = m_assets[item.id];
        dst.peaks100 = p100;
        dst.peaks10 = p10;
        dst.peaks1 = p1;
        dst.wavesReady = true;
    }
    QMetaObject::invokeMethod(this, [this, id = item.id] { emit updated(id); }, Qt::QueuedConnection);
}

}  // namespace hr
