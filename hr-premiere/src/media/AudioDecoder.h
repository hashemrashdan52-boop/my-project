#pragma once
#include <QString>
#include <vector>

struct AVFormatContext;
struct AVCodecContext;
struct AVFrame;
struct AVPacket;
struct SwrContext;

namespace hr {

// Decodes the first audio stream of a file to 48 kHz interleaved stereo float.
// Sequential reads are free; any other read seeks.  Not thread safe.
class AudioDecoder {
public:
    static constexpr int kRate = 48000;

    AudioDecoder();
    ~AudioDecoder();
    AudioDecoder(const AudioDecoder&) = delete;
    AudioDecoder& operator=(const AudioDecoder&) = delete;

    bool open(const QString& path);
    void close();
    bool isOpen() const { return m_fmt != nullptr; }
    double duration() const { return m_duration; }
    // Plays the file faster / slower (varispeed: the pitch follows). After this, read() positions are in
    // *playback* seconds: playback time t shows source time t * speed.
    void setSpeed(double speed);
    double speed() const { return m_speed; }

    // Fills `frames` stereo frames (2*frames floats) starting at source time t.
    // Anything outside the file is silence.
    void read(double t, float* out, int frames);

private:
    bool decodeOne();
    bool initResampler();
    void seekTo(qint64 sample);

    AVFormatContext* m_fmt = nullptr;
    AVCodecContext* m_ctx = nullptr;
    SwrContext* m_swr = nullptr;
    AVPacket* m_pkt = nullptr;
    AVFrame* m_frame = nullptr;
    int m_stream = -1;
    double m_timeBase = 0, m_startTime = 0, m_duration = 0;
    bool m_eof = false, m_draining = false;
    double m_speed = 1.0;

    std::vector<float> m_fifo;  // interleaved stereo
    qint64 m_fifoStart = 0;     // sample index of m_fifo[0]
    bool m_haveStart = false;   // m_fifoStart is meaningful
    bool m_positioned = false;  // a seek / first read happened
    std::vector<float> m_conv;
};

}  // namespace hr
