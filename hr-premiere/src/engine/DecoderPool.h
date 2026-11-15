#pragma once
#include "media/AudioDecoder.h"
#include "media/VideoDecoder.h"

#include <QHash>
#include <QImage>
#include <QSet>
#include <memory>

namespace hr {

// Per-thread cache of open decoders.  Decoders are keyed by (file, track) so that
// consecutive clips cut from the same file on the same track keep decoding
// sequentially instead of seeking at every cut.
class DecoderPool {
public:
    VideoDecoder* video(const QString& key, const QString& path);
    AudioDecoder* audio(const QString& key, const QString& path);
    // Still image, downscaled to the smallest cached mip that is at least wantWidth wide.
    QImage still(const QString& path, int wantWidth);
    bool isOffline(const QString& path) const { return m_failed.contains(path); }
    void clear();

private:
    struct V { std::shared_ptr<VideoDecoder> dec; QString path; quint64 use = 0; };
    struct A { std::shared_ptr<AudioDecoder> dec; QString path; quint64 use = 0; };
    struct S { QVector<QImage> mips; quint64 use = 0; };
    QHash<QString, V> m_video;
    QHash<QString, A> m_audio;
    QHash<QString, S> m_stills;
    QSet<QString> m_failed;
    quint64 m_tick = 0;
};

}  // namespace hr
