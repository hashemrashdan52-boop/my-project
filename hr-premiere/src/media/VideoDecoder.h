#pragma once
#include <QImage>
#include <QSize>
#include <QString>

struct AVFormatContext;
struct AVCodecContext;
struct AVFrame;
struct AVPacket;
struct SwsContext;

namespace hr {

// Sequential/seekable video decoder.  Not thread safe: every thread owns its own
// decoders (see DecoderPool).
class VideoDecoder {
public:
    VideoDecoder();
    ~VideoDecoder();
    VideoDecoder(const VideoDecoder&) = delete;
    VideoDecoder& operator=(const VideoDecoder&) = delete;

    bool open(const QString& path);
    void close();
    bool isOpen() const { return m_fmt != nullptr; }

    double duration() const { return m_duration; }
    double fps() const { return m_fps; }
    QSize nativeSize() const { return QSize(m_w, m_h); }  // coded size (before rotation)
    int rotation() const { return m_rotation; }           // clockwise degrees to display upright

    // The frame that is on screen at time t (seconds from the start of the file),
    // scaled down to fit maxSize (aspect ratio kept, never upscaled; invalid = native).
    // The image is NOT rotated – apply rotation() yourself (or use uprightFrameAt).
    QImage frameAt(double t, QSize maxSize = QSize());
    QImage uprightFrameAt(double t, QSize maxSize = QSize());

private:
    bool decodeInto(AVFrame* frame, double& pts);
    void seekTo(double t);
    QImage convert(AVFrame* f, QSize maxSize);

    AVFormatContext* m_fmt = nullptr;
    AVCodecContext* m_ctx = nullptr;
    AVPacket* m_pkt = nullptr;
    AVFrame* m_cur = nullptr;
    AVFrame* m_next = nullptr;
    SwsContext* m_sws = nullptr;
    int m_stream = -1;
    int m_w = 0, m_h = 0, m_rotation = 0;
    double m_timeBase = 0, m_startTime = 0, m_duration = 0, m_fps = 25;
    double m_curPts = 0, m_nextPts = 0, m_lastPts = 0;
    bool m_curValid = false, m_nextValid = false, m_eof = false, m_draining = false;

    QImage m_cache;
    double m_cachePts = -1;
    QSize m_cacheMax;
};

}  // namespace hr
