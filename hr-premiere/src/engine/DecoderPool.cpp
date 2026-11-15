#include "DecoderPool.h"

namespace hr {

template <typename Map>
static void evict(Map& map, int limit) {
    while (map.size() > limit) {
        auto victim = map.begin();
        for (auto it = map.begin(); it != map.end(); ++it)
            if (it->use < victim->use) victim = it;
        map.erase(victim);
    }
}

VideoDecoder* DecoderPool::video(const QString& key, const QString& path) {
    auto it = m_video.find(key);
    if (it != m_video.end() && it->path == path) {
        it->use = ++m_tick;
        return it->dec.get();
    }
    if (m_failed.contains(path)) return nullptr;
    auto dec = std::make_shared<VideoDecoder>();
    if (!dec->open(path)) {
        m_failed.insert(path);
        return nullptr;
    }
    V v;
    v.dec = std::move(dec);
    v.path = path;
    v.use = ++m_tick;
    VideoDecoder* raw = v.dec.get();
    m_video.insert(key, std::move(v));
    evict(m_video, 10);
    return raw;
}

AudioDecoder* DecoderPool::audio(const QString& key, const QString& path) {
    auto it = m_audio.find(key);
    if (it != m_audio.end() && it->path == path) {
        it->use = ++m_tick;
        return it->dec.get();
    }
    if (m_failed.contains(path)) return nullptr;
    auto dec = std::make_shared<AudioDecoder>();
    if (!dec->open(path)) {
        m_failed.insert(path);
        return nullptr;
    }
    A a;
    a.dec = std::move(dec);
    a.path = path;
    a.use = ++m_tick;
    AudioDecoder* raw = a.dec.get();
    m_audio.insert(key, std::move(a));
    evict(m_audio, 10);
    return raw;
}

QImage DecoderPool::still(const QString& path, int wantWidth) {
    auto it = m_stills.find(path);
    if (it == m_stills.end()) {
        if (m_failed.contains(path)) return {};
        QImage img(path);
        if (img.isNull()) {
            // formats Qt cannot read (but FFmpeg can): take the first video frame
            VideoDecoder d;
            if (d.open(path)) img = d.uprightFrameAt(0);
        }
        if (img.isNull()) {
            m_failed.insert(path);
            return {};
        }
        if (img.width() > 4096 || img.height() > 4096)
            img = img.scaled(4096, 4096, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        img = img.convertToFormat(QImage::Format_ARGB32_Premultiplied);
        S s;
        s.use = ++m_tick;
        s.mips.push_back(img);
        while (s.mips.last().width() > 256 && s.mips.last().height() > 256) {
            const QImage& p = s.mips.last();
            s.mips.push_back(p.scaled(p.width() / 2, p.height() / 2, Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
        }
        it = m_stills.insert(path, std::move(s));
        evict(m_stills, 12);
        it = m_stills.find(path);
    }
    it->use = ++m_tick;
    for (int i = it->mips.size() - 1; i >= 0; --i)
        if (it->mips[i].width() >= wantWidth) return it->mips[i];
    return it->mips.first();
}

void DecoderPool::clear() {
    m_video.clear();
    m_audio.clear();
    m_stills.clear();
    m_failed.clear();
}

}  // namespace hr
