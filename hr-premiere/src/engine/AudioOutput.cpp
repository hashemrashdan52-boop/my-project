#include "AudioOutput.h"

#include <QAudioDevice>
#include <QMediaDevices>
#include <chrono>
#include <cmath>
#include <cstring>

namespace hr {

static qint64 steadyNs() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

// QIODevice the sink pulls from.
class MixerDevice : public QIODevice {
public:
    MixerDevice(AudioOutput* o) : m_o(o) { open(QIODevice::ReadOnly); }
    void reset(qint64 startSample) { m_sample = startSample; }
    bool isSequential() const override { return true; }
    qint64 bytesAvailable() const override { return (1 << 20) + QIODevice::bytesAvailable(); }
    qint64 writeData(const char*, qint64) override { return -1; }
    qint64 readData(char* data, qint64 maxlen) override {
        const int bytesPerFrame = m_o->m_float ? 8 : 4;
        // The sink asks for as much as it has room for: give it all of that (capped), in small blocks.
        qint64 remaining = std::min<qint64>(maxlen / bytesPerFrame, 16384);
        if (remaining <= 0) return 0;
        std::shared_ptr<const Timeline> tl;
        {
            QMutexLocker l(&m_o->m_tlMutex);
            tl = m_o->m_timeline;
        }
        qint64 written = 0;
        while (remaining > 0) {
            const int frames = int(std::min<qint64>(remaining, 4096));
            m_buf.resize(size_t(frames) * 2);
            if (tl) m_o->m_mixer->render(*tl, m_sample, frames, m_buf.data());
            else std::fill(m_buf.begin(), m_buf.end(), 0.0f);
            m_sample += frames;
            char* dst = data + written;
            if (m_o->m_float) {
                std::memcpy(dst, m_buf.data(), size_t(frames) * 8);
            } else {
                int16_t* d = reinterpret_cast<int16_t*>(dst);
                for (size_t i = 0; i < m_buf.size(); ++i) d[i] = int16_t(std::lround(m_buf[i] * 32767.0f));
            }
            // simple peak hold with decay for the meters
            const float l = m_o->m_mixer->peakL(), r = m_o->m_mixer->peakR();
            m_o->m_levelL.store(std::max(l, m_o->m_levelL.load() * 0.9f));
            m_o->m_levelR.store(std::max(r, m_o->m_levelR.load() * 0.9f));
            written += qint64(frames) * bytesPerFrame;
            remaining -= frames;
        }
        return written;
    }

private:
    AudioOutput* m_o;
    qint64 m_sample = 0;
    std::vector<float> m_buf;
};

AudioOutput::AudioOutput(QObject* parent) : QObject(parent) {
    m_mixer = std::make_unique<AudioMixer>(&m_pool);
    moveToThread(&m_thread);
    m_thread.start();
    QMetaObject::invokeMethod(this, [this] {
        m_timer = new QTimer(this);
        m_timer->setInterval(10);
        connect(m_timer, &QTimer::timeout, this, &AudioOutput::updateClock);
    }, Qt::BlockingQueuedConnection);
}

AudioOutput::~AudioOutput() {
    QMetaObject::invokeMethod(this, [this] {
        if (m_timer) m_timer->stop();
        m_sink.reset();
        m_device.reset();
    }, Qt::BlockingQueuedConnection);
    m_thread.quit();
    m_thread.wait();
}

void AudioOutput::start(std::shared_ptr<const Timeline> tl, double fromTime) {
    QMetaObject::invokeMethod(this, [this, tl, fromTime] { doStart(tl, fromTime); }, Qt::QueuedConnection);
}
void AudioOutput::stop() {
    m_active.store(false);
    QMetaObject::invokeMethod(this, [this] { doStop(); }, Qt::QueuedConnection);
}
void AudioOutput::setTimeline(std::shared_ptr<const Timeline> tl) {
    QMutexLocker l(&m_tlMutex);
    m_timeline = std::move(tl);
}

double AudioOutput::clock() const {
    const double base = m_clockTime.load();
    const double dt = double(steadyNs() - m_clockWallNs.load()) / 1e9;
    return base + std::min(dt, 0.05);
}

void AudioOutput::doStart(std::shared_ptr<const Timeline> tl, double fromTime) {
    doStop();
    {
        QMutexLocker l(&m_tlMutex);
        m_timeline = tl;
    }
    m_from = fromTime;
    const QAudioDevice dev = QMediaDevices::defaultAudioOutput();
    if (dev.isNull()) return;
    QAudioFormat fmt;
    fmt.setSampleRate(AudioMixer::kRate);
    fmt.setChannelCount(2);
    fmt.setSampleFormat(QAudioFormat::Float);
    m_float = true;
    if (!dev.isFormatSupported(fmt)) {
        fmt.setSampleFormat(QAudioFormat::Int16);
        m_float = false;
        if (!dev.isFormatSupported(fmt)) return;
    }
    m_sink = std::make_unique<QAudioSink>(dev, fmt);
    m_sink->setBufferSize(AudioMixer::kRate / 10 * (m_float ? 8 : 4));  // ~100 ms keeps picture and sound close together
    m_device = std::make_unique<MixerDevice>(this);
    m_device->reset(qint64(std::llround(fromTime * AudioMixer::kRate)));
    m_clockTime.store(fromTime);
    m_clockWallNs.store(steadyNs());
    m_sink->start(m_device.get());
    if (m_sink->error() != QAudio::NoError) {
        m_sink.reset();
        m_device.reset();
        return;
    }
    m_active.store(true);
    m_timer->start();
}

void AudioOutput::doStop() {
    if (m_timer) m_timer->stop();
    m_active.store(false);
    if (m_sink) m_sink->stop();
    m_sink.reset();
    m_device.reset();
    m_levelL.store(0);
    m_levelR.store(0);
}

void AudioOutput::updateClock() {
    if (!m_sink) return;
    // HR_AUDIO_OFFSET_MS lets you nudge the picture against the sound on devices whose clock is early / late
    static const double offset = qEnvironmentVariable("HR_AUDIO_OFFSET_MS", "0").toDouble() / 1000.0;
    m_clockTime.store(m_from + double(m_sink->processedUSecs()) / 1e6 + offset);
    m_clockWallNs.store(steadyNs());
}

}  // namespace hr
