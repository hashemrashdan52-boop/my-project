#include "Player.h"

#include "core/Util.h"

#include <chrono>
#include <cmath>

namespace hr {

static qint64 steadyNs() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

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
        m_restarts = 0;
        if (m_useAudioClock) restartAudio(t);
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
    m_useAudioClock = !m_muteAudio && timelineHasAudio();
    m_restarts = 0;
    if (m_useAudioClock) restartAudio(m_pos);
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

void Player::restartAudio(double picturePos) {
    // The sound is started `m_lead` seconds ahead of the picture so that, once the device has
    // finished its start-up latency, what you hear matches what you see.
    m_audioFrom = std::max(0.0, picturePos + m_lead);
    m_audio.start(m_tl, m_audioFrom);
    m_audioMoving = false;
    m_lastAc = m_audioFrom;
    m_lastAcNs = m_lastRestartNs = steadyNs();
}

void Player::tick() {
    // The picture runs on a smooth wall clock.  The audio clock is the truth about what is audible, so
    // the picture clock is nudged towards it.  While the device has not started yet (or stalls) the
    // picture simply keeps going; if the sound turns out to be far off (a device with a large
    // latency, e.g. Bluetooth) the audio stream is restarted earlier by the measured offset.
    double wallT = m_playStart + m_wall.nsecsElapsed() / 1e9;
    if (m_useAudioClock && m_audio.isActive()) {
        const qint64 nowNs = steadyNs();
        const double ac = m_audio.clock();
        const double since = double(nowNs - m_lastAcNs) / 1e9;
        if (since >= 0.15) {
            const double rate = (ac - m_lastAc) / since;
            m_audioMoving = rate > 0.6 && rate < 1.6;
            m_lastAc = ac;
            m_lastAcNs = nowNs;
        }
        if (m_audioMoving) {
            const double drift = ac - wallT;  // > 0: the sound is ahead of the picture
            m_lastDrift = drift;
            if (std::fabs(drift) < 0.25) {
                m_playStart += drift * 0.05;
                wallT += drift * 0.05;
            } else if (m_restarts < 3 && nowNs - m_lastRestartNs > 1000000000LL) {
                m_lead = std::max(-3.0, std::min(3.0, m_lead - drift));
                ++m_restarts;
                restartAudio(wallT);
            }
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
