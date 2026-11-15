#include "VideoDecoder.h"

#include "FFmpegUtil.h"

#include <cmath>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/display.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

namespace hr {

VideoDecoder::VideoDecoder() {}
VideoDecoder::~VideoDecoder() { close(); }

void VideoDecoder::close() {
    if (m_sws) { sws_freeContext(m_sws); m_sws = nullptr; }
    if (m_cur) av_frame_free(&m_cur);
    if (m_next) av_frame_free(&m_next);
    if (m_pkt) av_packet_free(&m_pkt);
    if (m_ctx) avcodec_free_context(&m_ctx);
    if (m_fmt) avformat_close_input(&m_fmt);
    m_stream = -1;
    m_curValid = m_nextValid = m_eof = m_draining = false;
    m_cache = QImage();
    m_cachePts = -1;
}

bool VideoDecoder::open(const QString& path) {
    close();
    if (avformat_open_input(&m_fmt, path.toUtf8().constData(), nullptr, nullptr) < 0) { m_fmt = nullptr; return false; }
    if (avformat_find_stream_info(m_fmt, nullptr) < 0) { close(); return false; }

    m_stream = av_find_best_stream(m_fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (m_stream < 0) { close(); return false; }
    AVStream* st = m_fmt->streams[m_stream];
    const AVCodec* codec = avcodec_find_decoder(st->codecpar->codec_id);
    if (!codec) { close(); return false; }
    m_ctx = avcodec_alloc_context3(codec);
    if (!m_ctx || avcodec_parameters_to_context(m_ctx, st->codecpar) < 0) { close(); return false; }
    m_ctx->thread_count = 4;
    m_ctx->thread_type = FF_THREAD_FRAME | FF_THREAD_SLICE;
    if (avcodec_open2(m_ctx, codec, nullptr) < 0) { close(); return false; }

    m_pkt = av_packet_alloc();
    m_cur = av_frame_alloc();
    m_next = av_frame_alloc();
    m_w = m_ctx->width;
    m_h = m_ctx->height;
    m_timeBase = av_q2d(st->time_base);
    m_startTime = m_fmt->start_time == AV_NOPTS_VALUE ? 0.0 : double(m_fmt->start_time) / AV_TIME_BASE;
    const AVRational fr = av_guess_frame_rate(m_fmt, st, nullptr);
    m_fps = (fr.num > 0 && fr.den > 0) ? av_q2d(fr) : 25.0;
    if (m_fps < 1 || m_fps > 480) m_fps = 25.0;

    if (m_fmt->duration > 0) m_duration = double(m_fmt->duration) / AV_TIME_BASE;
    else if (st->duration > 0) m_duration = st->duration * m_timeBase;
    else m_duration = 0;

    m_rotation = 0;
    const AVPacketSideData* sd = av_packet_side_data_get(st->codecpar->coded_side_data,
                                                         st->codecpar->nb_coded_side_data,
                                                         AV_PKT_DATA_DISPLAYMATRIX);
    if (sd && sd->size >= 9 * int(sizeof(int32_t))) {
        const double theta = av_display_rotation_get(reinterpret_cast<const int32_t*>(sd->data));
        if (!std::isnan(theta)) {
            int rot = int(std::lround(-theta));
            rot = ((rot % 360) + 360) % 360;
            m_rotation = (rot / 90) * 90;
        }
    }
    return true;
}

// Decodes the next frame of the video stream. pts is in seconds from the file start.
bool VideoDecoder::decodeInto(AVFrame* frame, double& pts) {
    for (;;) {
        const int r = avcodec_receive_frame(m_ctx, frame);
        if (r == 0) {
            int64_t ts = frame->best_effort_timestamp;
            if (ts == AV_NOPTS_VALUE) pts = m_lastPts + 1.0 / m_fps;
            else pts = double(ts) * m_timeBase - m_startTime;
            m_lastPts = pts;
            return true;
        }
        if (r == AVERROR_EOF) return false;
        if (r != AVERROR(EAGAIN)) return false;
        if (m_draining) return false;
        const int rr = av_read_frame(m_fmt, m_pkt);
        if (rr < 0) {
            avcodec_send_packet(m_ctx, nullptr);
            m_draining = true;
            continue;
        }
        if (m_pkt->stream_index == m_stream) avcodec_send_packet(m_ctx, m_pkt);
        av_packet_unref(m_pkt);
    }
}

void VideoDecoder::seekTo(double t) {
    const int64_t ts = int64_t((t + m_startTime) / m_timeBase);
    if (av_seek_frame(m_fmt, m_stream, ts, AVSEEK_FLAG_BACKWARD) < 0)
        avformat_seek_file(m_fmt, m_stream, INT64_MIN, ts, ts, 0);
    avcodec_flush_buffers(m_ctx);
    m_draining = false;
    m_eof = false;
    m_curValid = m_nextValid = false;
    av_frame_unref(m_cur);
    av_frame_unref(m_next);
    m_lastPts = t;
}

QImage VideoDecoder::convert(AVFrame* f, QSize maxSize) {
    const int sw = f->width, sh = f->height;
    double scale = 1.0;
    if (maxSize.isValid() && !maxSize.isEmpty())
        scale = std::min(1.0, std::min(double(maxSize.width()) / sw, double(maxSize.height()) / sh));
    int dw = std::max(2, int(std::lround(sw * scale)) & ~1);
    int dh = std::max(2, int(std::lround(sh * scale)) & ~1);
    if (scale >= 1.0) { dw = sw; dh = sh; }

    m_sws = sws_getCachedContext(m_sws, sw, sh, AVPixelFormat(f->format), dw, dh, AV_PIX_FMT_BGRA,
                                 scale < 1.0 ? SWS_AREA : SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (!m_sws) return {};
    const int cs = (f->colorspace == AVCOL_SPC_BT709 ||
                    (f->colorspace == AVCOL_SPC_UNSPECIFIED && sh >= 720)) ? SWS_CS_ITU709 : SWS_CS_ITU601;
    const int* coef = sws_getCoefficients(cs);
    const int srcRange = f->color_range == AVCOL_RANGE_JPEG ? 1 : 0;
    sws_setColorspaceDetails(m_sws, coef, srcRange, coef, 1, 0, 1 << 16, 1 << 16);

    QImage img(dw, dh, QImage::Format_RGB32);
    uint8_t* dst[4] = {img.bits(), nullptr, nullptr, nullptr};
    int dstStride[4] = {int(img.bytesPerLine()), 0, 0, 0};
    sws_scale(m_sws, f->data, f->linesize, 0, sh, dst, dstStride);
    return img;
}

QImage VideoDecoder::frameAt(double t, QSize maxSize) {
    if (!isOpen()) return {};
    const double fd = 1.0 / m_fps;
    if (m_duration > 0) t = std::min(t, std::max(0.0, m_duration - 0.5 * fd));
    t = std::max(0.0, t);

    const bool needSeek = !m_curValid || t < m_curPts - 1e-4 ||
                          t > (m_nextValid ? m_nextPts : m_curPts) + 1.0;
    if (needSeek) seekTo(t);

    for (;;) {
        if (!m_curValid) {
            double pts;
            if (!decodeInto(m_cur, pts)) { m_eof = true; break; }
            m_curPts = pts;
            m_curValid = true;
        }
        if (!m_nextValid && !m_eof) {
            double pts;
            if (decodeInto(m_next, pts)) { m_nextPts = pts; m_nextValid = true; }
            else m_eof = true;
        }
        if (m_nextValid && m_nextPts <= t + 1e-4) {
            std::swap(m_cur, m_next);
            m_curPts = m_nextPts;
            m_nextValid = false;
            continue;
        }
        break;
    }
    if (!m_curValid) return m_cache;

    if (!m_cache.isNull() && std::abs(m_cachePts - m_curPts) < 1e-9 && m_cacheMax == maxSize) return m_cache;
    m_cache = convert(m_cur, maxSize);
    m_cachePts = m_curPts;
    m_cacheMax = maxSize;
    return m_cache;
}

QImage VideoDecoder::uprightFrameAt(double t, QSize maxSize) {
    // note: maxSize applies before rotation; callers use square-ish limits for thumbnails
    QImage img = frameAt(t, m_rotation % 180 ? QSize(maxSize.height(), maxSize.width()) : maxSize);
    return rotateImage(img, m_rotation);
}

}  // namespace hr
