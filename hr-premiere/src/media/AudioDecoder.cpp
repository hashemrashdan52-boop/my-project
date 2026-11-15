#include "AudioDecoder.h"

#include <algorithm>
#include <cmath>
#include <cstring>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>
}

namespace hr {

AudioDecoder::AudioDecoder() {}
AudioDecoder::~AudioDecoder() { close(); }

void AudioDecoder::close() {
    if (m_swr) swr_free(&m_swr);
    if (m_frame) av_frame_free(&m_frame);
    if (m_pkt) av_packet_free(&m_pkt);
    if (m_ctx) avcodec_free_context(&m_ctx);
    if (m_fmt) avformat_close_input(&m_fmt);
    m_stream = -1;
    m_fifo.clear();
    m_haveStart = m_positioned = m_eof = m_draining = false;
}

bool AudioDecoder::open(const QString& path) {
    close();
    if (avformat_open_input(&m_fmt, path.toUtf8().constData(), nullptr, nullptr) < 0) { m_fmt = nullptr; return false; }
    if (avformat_find_stream_info(m_fmt, nullptr) < 0) { close(); return false; }
    m_stream = av_find_best_stream(m_fmt, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (m_stream < 0) { close(); return false; }
    AVStream* st = m_fmt->streams[m_stream];
    const AVCodec* codec = avcodec_find_decoder(st->codecpar->codec_id);
    if (!codec) { close(); return false; }
    m_ctx = avcodec_alloc_context3(codec);
    if (!m_ctx || avcodec_parameters_to_context(m_ctx, st->codecpar) < 0) { close(); return false; }
    if (avcodec_open2(m_ctx, codec, nullptr) < 0) { close(); return false; }

    if (!initResampler()) { close(); return false; }

    m_pkt = av_packet_alloc();
    m_frame = av_frame_alloc();
    m_timeBase = av_q2d(st->time_base);
    m_startTime = m_fmt->start_time == AV_NOPTS_VALUE ? 0.0 : double(m_fmt->start_time) / AV_TIME_BASE;
    if (m_fmt->duration > 0) m_duration = double(m_fmt->duration) / AV_TIME_BASE;
    else if (st->duration > 0) m_duration = st->duration * m_timeBase;
    return true;
}

bool AudioDecoder::initResampler() {
    if (m_swr) swr_free(&m_swr);
    AVChannelLayout in{};
    if (m_ctx->ch_layout.order == AV_CHANNEL_ORDER_UNSPEC || m_ctx->ch_layout.nb_channels == 0)
        av_channel_layout_default(&in, std::max(1, m_ctx->ch_layout.nb_channels ? m_ctx->ch_layout.nb_channels : 2));
    else
        av_channel_layout_copy(&in, &m_ctx->ch_layout);
    AVChannelLayout out{};
    av_channel_layout_default(&out, 2);
    // speed: tell the resampler the file is recorded at a different rate, it then plays it faster / slower
    const int inRate = std::max(1000, int(std::lround(double(m_ctx->sample_rate) * m_speed)));
    const int r = swr_alloc_set_opts2(&m_swr, &out, AV_SAMPLE_FMT_FLT, kRate, &in, m_ctx->sample_fmt, inRate, 0, nullptr);
    av_channel_layout_uninit(&in);
    av_channel_layout_uninit(&out);
    return r >= 0 && m_swr && swr_init(m_swr) >= 0;
}

void AudioDecoder::setSpeed(double speed) {
    speed = std::max(0.05, std::min(speed, 20.0));
    if (std::abs(speed - m_speed) < 1e-9) return;
    m_speed = speed;
    if (!isOpen()) return;
    initResampler();
    m_fifo.clear();
    m_haveStart = false;
    m_positioned = false;
}

void AudioDecoder::seekTo(qint64 sample) {
    const double t = double(sample) / kRate * m_speed;
    const int64_t ts = int64_t((t + m_startTime) / m_timeBase);
    if (av_seek_frame(m_fmt, m_stream, ts, AVSEEK_FLAG_BACKWARD) < 0)
        avformat_seek_file(m_fmt, m_stream, INT64_MIN, ts, ts, 0);
    avcodec_flush_buffers(m_ctx);
    swr_close(m_swr);
    swr_init(m_swr);
    m_fifo.clear();
    m_haveStart = false;
    m_eof = false;
    m_draining = false;
    m_positioned = true;
    m_fifoStart = sample;
}

// Decodes one frame into the FIFO. Returns false at end of stream.
bool AudioDecoder::decodeOne() {
    for (;;) {
        const int r = avcodec_receive_frame(m_ctx, m_frame);
        if (r == 0) {
            const int maxOut = swr_get_out_samples(m_swr, m_frame->nb_samples) + 64;
            m_conv.resize(size_t(maxOut) * 2);
            uint8_t* outPtr = reinterpret_cast<uint8_t*>(m_conv.data());
            const int n = swr_convert(m_swr, &outPtr, maxOut,
                                      const_cast<const uint8_t**>(m_frame->extended_data), m_frame->nb_samples);
            if (n > 0) {
                if (!m_haveStart) {
                    int64_t ts = m_frame->best_effort_timestamp;
                    if (ts != AV_NOPTS_VALUE) {
                        const double sec = double(ts) * m_timeBase - m_startTime;
                        m_fifoStart = std::llround(sec / m_speed * kRate);
                    }
                    m_haveStart = true;
                }
                m_fifo.insert(m_fifo.end(), m_conv.begin(), m_conv.begin() + size_t(n) * 2);
            }
            av_frame_unref(m_frame);
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

void AudioDecoder::read(double t, float* out, int frames) {
    if (frames <= 0) return;
    std::memset(out, 0, sizeof(float) * size_t(frames) * 2);
    if (!isOpen()) return;
    const qint64 s0 = std::llround(t * kRate);
    const qint64 s1 = s0 + frames;

    qint64 fifoEnd = m_fifoStart + qint64(m_fifo.size() / 2);
    const bool contiguous = m_positioned && s0 >= m_fifoStart && s0 <= fifoEnd + kRate / 2;
    if (!contiguous) {
        if (s1 <= 0) return;  // entirely before the start of the file
        seekTo(std::max<qint64>(0, s0));
        fifoEnd = m_fifoStart;
    }

    while (!m_eof) {
        fifoEnd = m_fifoStart + qint64(m_fifo.size() / 2);
        if (m_haveStart && fifoEnd >= s1) break;
        if (!decodeOne()) { m_eof = true; break; }
    }

    fifoEnd = m_fifoStart + qint64(m_fifo.size() / 2);
    for (int i = 0; i < frames; ++i) {
        const qint64 s = s0 + i;
        if (s < m_fifoStart || s >= fifoEnd) continue;
        const size_t k = size_t(s - m_fifoStart) * 2;
        out[2 * i] = m_fifo[k];
        out[2 * i + 1] = m_fifo[k + 1];
    }
    // drop everything we have consumed
    if (s1 > m_fifoStart) {
        const qint64 drop = std::min<qint64>(s1 - m_fifoStart, qint64(m_fifo.size() / 2));
        m_fifo.erase(m_fifo.begin(), m_fifo.begin() + size_t(drop) * 2);
        m_fifoStart += drop;
        if (m_fifo.empty() && m_fifoStart < s1) m_fifoStart = s1;
    }
}

}  // namespace hr
