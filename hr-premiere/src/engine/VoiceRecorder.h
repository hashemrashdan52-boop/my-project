#pragma once
#include <QAudioFormat>
#include <QAudioSource>
#include <QFile>
#include <QObject>
#include <memory>

namespace hr {

// Records the default microphone into a 16-bit PCM WAV file (used for voice-overs).
class VoiceRecorder : public QObject {
    Q_OBJECT
public:
    explicit VoiceRecorder(QObject* parent = nullptr);
    ~VoiceRecorder() override;

    static bool hasMicrophone();   // an audio input device exists
    static QString deviceName();

    bool start(const QString& wavPath, QString* error = nullptr);
    // Stops and finalises the file. Returns the recorded length in seconds.
    double stop();
    bool isRecording() const { return m_source != nullptr; }
    double seconds() const;

signals:
    void level(float peak);                 // 0..1, about every 20 ms while recording
    void failed(const QString& message);

private slots:
    void onReadyRead();

private:
    void writeHeader();

    std::unique_ptr<QAudioSource> m_source;
    QIODevice* m_io = nullptr;
    QFile m_file;
    QAudioFormat m_format;
    qint64 m_dataBytes = 0;   // bytes of PCM16 data written
};

}  // namespace hr
