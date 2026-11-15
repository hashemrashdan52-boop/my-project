#pragma once
#include "AudioOutput.h"
#include "FrameServer.h"

#include <QElapsedTimer>
#include <QObject>
#include <QTimer>

namespace hr {

// Transport for one monitor: owns a frame-rendering thread and an audio output, keeps
// the playhead and drives playback from the audio clock (or a wall clock when the
// timeline has no audio / no audio device is available).
class Player : public QObject {
    Q_OBJECT
public:
    explicit Player(QObject* parent = nullptr);
    ~Player() override;

    void setTimeline(std::shared_ptr<const Timeline> tl);
    const Timeline& timeline() const { return *m_tl; }
    void setViewSize(QSize s);

    double position() const { return m_pos; }
    double duration() const { return m_tl->duration(); }
    double fps() const { return m_tl->fps; }
    bool isPlaying() const { return m_playing; }

public slots:
    void seek(double t);
    void play();
    void pause();
    void toggle();
    void stepFrames(int n);
    void refresh();  // re-render the current frame (after an edit)

signals:
    void positionChanged(double t);
    void playingChanged(bool playing);
    void frameReady(const QImage& image, double t);
    void levels(float left, float right);

private slots:
    void tick();

private:
    void requestFrame(double t, bool force);
    bool timelineHasAudio() const;

    std::shared_ptr<const Timeline> m_tl;
    FrameServer m_server;
    AudioOutput m_audio;
    QTimer m_timer;
    QElapsedTimer m_wall;
    double m_pos = 0;
    double m_playStart = 0;
    bool m_playing = false;
    bool m_useAudioClock = false;
    bool m_audioSynced = false;
    QSize m_size{640, 360};
    qint64 m_lastFrameIdx = -1;
};

}  // namespace hr
