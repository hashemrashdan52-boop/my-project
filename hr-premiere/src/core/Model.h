#pragma once
#include "Effects.h"

#include <QColor>
#include <QHash>
#include <QSet>
#include <QString>
#include <QVector>

namespace hr {

enum class MediaKind { Video, Audio, Image };
enum class ClipKind { Media, Title };
enum class TrackType { Video, Audio };

QString newId();

// One imported file in the Project panel.
struct MediaItem {
    QString id;
    QString name;
    QString path;
    MediaKind kind = MediaKind::Video;
    double duration = 0;   // seconds (images: default still duration)
    int width = 0, height = 0;
    double fps = 0;
    bool hasVideo = false, hasAudio = false;
    int sampleRate = 0, channels = 0;
    double markIn = -1, markOut = -1;  // Source monitor marks (-1 = not set)
    bool offline = false;
};

// A clip placed on a track.  The renderer works on copies of the Timeline in other
// threads, so a clip carries everything needed to draw/play it (path, kind, ...).
struct Clip {
    QString id;
    ClipKind kind = ClipKind::Media;
    QString mediaId;
    QString path;
    MediaKind mediaKind = MediaKind::Video;  // which stream of `path` this clip uses
    QString name;
    QString linkId;          // clips sharing a linkId move / trim / split together
    double start = 0;        // position on the timeline (s)
    double duration = 0;     // length on the timeline (s)
    double srcIn = 0;        // in-point inside the source (s)
    double sourceLength = 0; // total source length, 0 = unbounded (stills, titles)
    bool enabled = true;
    QVector<EffectInstance> effects;

    double end() const { return start + duration; }
    EffectInstance* effect(const QString& type);
    const EffectInstance* effect(const QString& type) const;
    bool hasCustomEffects() const;  // anything besides the intrinsic ones
};

struct Track {
    TrackType type = TrackType::Video;
    QString name;
    bool enabled = true;  // video: eye (visible)   audio: not muted
    bool locked = false;
    bool solo = false;    // audio only
    QVector<Clip> clips;  // always sorted by start, never overlapping
};

struct Marker {
    double time = 0;
    QString name;
    QColor color = QColor("#7bd88f");
};

struct TimeRange {
    double start = 0, end = 0;
    double length() const { return end - start; }
};

struct ClipRef {
    TrackType type = TrackType::Video;
    int track = -1;
    int index = -1;
    bool valid() const { return track >= 0 && index >= 0; }
};

// The sequence.  Plain value type (Qt containers are implicitly shared), so taking a
// snapshot for the render / audio threads or for the undo stack is cheap.
struct Timeline {
    QString name = QStringLiteral("Sequence 01");
    int width = 1920, height = 1080;
    double fps = 30.0;
    int sampleRate = 48000;
    QVector<Track> video;  // index 0 = V1 (bottom-most layer)
    QVector<Track> audio;  // index 0 = A1
    QVector<Marker> markers;
    double markIn = -1, markOut = -1;

    static Timeline makeDefault(int videoTracks = 3, int audioTracks = 3);

    double duration() const;
    bool isEmpty() const;
    ClipRef find(const QString& clipId) const;
    Clip* clip(const QString& clipId);
    const Clip* clip(const QString& clipId) const;
    QVector<Track>& tracks(TrackType t) { return t == TrackType::Video ? video : audio; }
    const QVector<Track>& tracks(TrackType t) const { return t == TrackType::Video ? video : audio; }
    Clip* clip(const ClipRef& r);
    const Clip* clip(const ClipRef& r) const;
    double frameDuration() const { return 1.0 / fps; }
};

}  // namespace hr
