#include "Player.h"

#include "core/Util.h"

#include <cmath>

namespace hr {

Player::Player(QObject* parent) : QObject(parent) {
    m_tl = std::make_shared<const Timeline>(Timeline::makeDefault());
    m_timer.setTimerType(Qt::PreciseTimer);
    m_timer.setInterval(8);
    connect(&m_timer, &QTimer::timeout, this, &Player::tick);
    connect(&m_server, &FrameServer::frameReady, this, &Player::frameReady, Qt::QueuedConnection);
}

Player::~Player() {
    m_timer.stop();
    m_server.shutdown();
}

void Player::setTimeline(std::shared_ptr<const Timeline> tl) {
    m_tl = std::move(tl);
    if (m_playing) m_audio.setTimeline(m_tl);
}

void Player::setViewSize(QSize s) {
    if (s.isEmpty() || s == m_size) return;
    m_size = s;
}

bool Player::timelineHasAudio() const {
    for (const Track& tr : m_tl->audio)
        if (tr.enabled && !tr.clips.isEmpty()) return true;
    return false;
}

void Player::requestFrame(double t, bool force) {
    const qint64 idx = qint64(std::floor(t * m_tl->fps + 1e-6));
    if (!force && idx == m_lastFrameIdx) return;
    m_lastFrameIdx = idx;
    m_server.request(m_tl, double(idx) / m_tl->fps, m_size);
}

void Player::refresh() { requestFrame(m_pos, true); }

void Player::seek(double t) {
    t = std::max(0.0, t);
    m_pos = t;
    if (m_playing) {
        m_playStart = t;
        m_wall.restart();
        m_audioSynced = false;
        if (m_useAudioClock) m_audio.start(m_tl, t);
    }
    emit positionChanged(m_pos);
    requestFrame(m_pos, !m_playing);
}

void Player::stepFrames(int n) {
    if (m_playing) pause();
    seek(snapToFrame(m_pos, m_tl->fps) + n / m_tl->fps);
}

void Player::play() {
    if (m_playing) return;
    const double dur = duration();
    if (dur <= 0) return;
    if (m_pos >= dur - 0.5 / m_tl->fps) m_pos = 0;
    m_playing = true;
    m_playStart = m_pos;
    m_wall.start();
    m_useAudioClock = timelineHasAudio();
    m_audioSynced = false;
    if (m_useAudioClock) m_audio.start(m_tl, m_pos);
    m_timer.start();
    emit playingChanged(true);
}

void Player::pause() {
    if (!m_playing) return;
    m_playing = false;
    m_timer.stop();
    m_audio.stop();
    emit playingChanged(false);
    emit levels(0, 0);
    requestFrame(m_pos, true);
}

void Player::toggle() {
    if (m_playing) pause();
    else play();
}

void Player::tick() {
    double wallT = m_playStart + m_wall.nsecsElapsed() / 1e9;
    if (m_useAudioClock && m_audio.isActive()) {
        // the wall clock is smooth; the audio clock is the truth.  Re-anchor when they drift apart
        // (this also makes the picture wait while the audio device is still starting up).
        const double ac = m_audio.clock();
        if (!m_audioSynced || std::fabs(wallT - ac) > 0.08) {
            m_playStart = ac;
            m_wall.restart();
            m_audioSynced = true;
            wallT = ac;
        }
    }
    const double t = wallT;
    const double dur = duration();
    if (t >= dur) {
        m_pos = dur;
        emit positionChanged(m_pos);
        pause();
        requestFrame(std::max(0.0, dur - 0.5 / m_tl->fps), true);
        return;
    }
    m_pos = t;
    emit positionChanged(m_pos);
    requestFrame(t, false);
    if (m_useAudioClock) emit levels(m_audio.levelL(), m_audio.levelR());
}

}  // namespace hr
