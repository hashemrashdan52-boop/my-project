#pragma once
#include "AudioMixer.h"

#include <QAudioSink>
#include <QElapsedTimer>
#include <QIODevice>
#include <QMutex>
#include <QObject>
#include <QThread>
#include <QTimer>
#include <atomic>
#include <memory>

namespace hr {

class MixerDevice;

// Plays a timeline through the default audio output.  Lives on its own thread: the
// sink pulls samples from the mixer, which decodes on that thread.
class AudioOutput : public QObject {
    Q_OBJECT
public:
    explicit AudioOutput(QObject* parent = nullptr);
    ~AudioOutput() override;

    // --- callable from any thread ---
    void start(std::shared_ptr<const Timeline> tl, double fromTime);
    void stop();
    void setTimeline(std::shared_ptr<const Timeline> tl);
    bool isActive() const { return m_active.load(); }
    // Timeline time currently being heard (valid while isActive()).
    double clock() const;
    float levelL() const { return m_levelL.load(); }
    float levelR() const { return m_levelR.load(); }

private slots:
    void updateClock();

private:
    void doStart(std::shared_ptr<const Timeline> tl, double fromTime);
    void doStop();
    friend class MixerDevice;
    QThread m_thread;
    DecoderPool m_pool;
    std::unique_ptr<AudioMixer> m_mixer;
    std::unique_ptr<QAudioSink> m_sink;
    std::unique_ptr<MixerDevice> m_device;
    QTimer* m_timer = nullptr;
    QMutex m_tlMutex;
    std::shared_ptr<const Timeline> m_timeline;
    double m_from = 0;
    bool m_float = true;

    std::atomic<bool> m_active{false};
    std::atomic<double> m_clockTime{0};
    std::atomic<qint64> m_clockWallNs{0};
    std::atomic<float> m_levelL{0}, m_levelR{0};
};

}  // namespace hr
