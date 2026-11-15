#include "EditOps.h"

#include <algorithm>
#include <limits>

namespace hr::ops {

static const double kMinDur = 1e-3;  // callers snap to frames; this is only a safety net

static void sortClips(Track& t) {
    std::stable_sort(t.clips.begin(), t.clips.end(),
                     [](const Clip& a, const Clip& b) { return a.start < b.start; });
}

QSet<QString> linkedIds(const Timeline& tl, const QString& id) {
    QSet<QString> out;
    out.insert(id);
    const Clip* c = tl.clip(id);
    if (!c || c->linkId.isEmpty()) return out;
    for (const auto* tracks : {&tl.video, &tl.audio})
        for (const auto& tr : *tracks)
            for (const auto& cl : tr.clips)
                if (cl.linkId == c->linkId) out.insert(cl.id);
    return out;
}

QSet<QString> expandLinked(const Timeline& tl, const QSet<QString>& ids) {
    QSet<QString> links, out = ids;
    for (const QString& id : ids)
        if (const Clip* c = tl.clip(id))
            if (!c->linkId.isEmpty()) links.insert(c->linkId);
    if (links.isEmpty()) return out;
    for (const auto* tracks : {&tl.video, &tl.audio})
        for (const auto& tr : *tracks)
            for (const auto& cl : tr.clips)
                if (links.contains(cl.linkId)) out.insert(cl.id);
    return out;
}

void carve(Track& track, double s, double e, const QSet<QString>& ignore) {
    QVector<Clip> out;
    out.reserve(track.clips.size() + 1);
    for (const Clip& c : track.clips) {
        if (ignore.contains(c.id) || c.end() <= s + kEps || c.start >= e - kEps) {
            out.push_back(c);
            continue;
        }
        const bool keepLeft = c.start < s - kEps;
        const bool keepRight = c.end() > e + kEps;
        if (keepLeft) {
            Clip left = c;
            left.duration = s - c.start;
            out.push_back(left);
        }
        if (keepRight) {
            Clip right = c;
            right.start = e;
            right.srcIn = c.srcIn + (e - c.start) * c.speed;
            right.duration = c.end() - e;
            if (keepLeft) right.id = newId();
            out.push_back(right);
        }
    }
    track.clips = out;
}

void place(Track& track, const Clip& clip) {
    for (int i = track.clips.size() - 1; i >= 0; --i)
        if (track.clips[i].id == clip.id) track.clips.removeAt(i);
    carve(track, clip.start, clip.end());
    track.clips.push_back(clip);
    sortClips(track);
}

// Splits clip c in place at time t, returns the new right half (same link mapping).
static Clip splitPiece(Clip& c, double t, QHash<QString, QString>& linkMap) {
    Clip right = c;
    right.id = newId();
    right.start = t;
    right.srcIn = c.srcIn + (t - c.start) * c.speed;
    right.duration = c.end() - t;
    c.duration = t - c.start;
    if (!c.linkId.isEmpty()) {
        if (!linkMap.contains(c.linkId)) linkMap.insert(c.linkId, newId());
        right.linkId = linkMap.value(c.linkId);
    }
    return right;
}

void insertSpace(Timeline& tl, double at, double dur) {
    QHash<QString, QString> linkMap;
    for (auto* tracks : {&tl.video, &tl.audio}) {
        for (Track& tr : *tracks) {
            if (tr.locked) continue;
            QVector<Clip> extra;
            for (Clip& c : tr.clips)
                if (c.start < at - kEps && c.end() > at + kEps) extra.push_back(splitPiece(c, at, linkMap));
            for (const Clip& c : extra) tr.clips.push_back(c);
            sortClips(tr);
            for (Clip& c : tr.clips)
                if (c.start >= at - kEps) c.start += dur;
        }
    }
}

bool splitAt(Timeline& tl, const QString& clipId, double t) {
    const Clip* c0 = tl.clip(clipId);
    if (!c0) return false;
    if (t <= c0->start + kMinDur || t >= c0->end() - kMinDur) return false;
    QHash<QString, QString> linkMap;
    bool any = false;
    const QSet<QString> ids = linkedIds(tl, clipId);
    for (auto* tracks : {&tl.video, &tl.audio}) {
        for (Track& tr : *tracks) {
            if (tr.locked) continue;
            QVector<Clip> extra;
            for (Clip& c : tr.clips) {
                if (!ids.contains(c.id)) continue;
                if (t > c.start + kMinDur && t < c.end() - kMinDur) {
                    extra.push_back(splitPiece(c, t, linkMap));
                    any = true;
                }
            }
            for (const Clip& c : extra) tr.clips.push_back(c);
            sortClips(tr);
        }
    }
    return any;
}

int splitAllAt(Timeline& tl, double t) {
    QHash<QString, QString> linkMap;
    int n = 0;
    for (auto* tracks : {&tl.video, &tl.audio}) {
        for (Track& tr : *tracks) {
            if (tr.locked) continue;
            QVector<Clip> extra;
            for (Clip& c : tr.clips)
                if (t > c.start + kMinDur && t < c.end() - kMinDur) {
                    extra.push_back(splitPiece(c, t, linkMap));
                    ++n;
                }
            for (const Clip& c : extra) tr.clips.push_back(c);
            sortClips(tr);
        }
    }
    return n;
}

void remove(Timeline& tl, const QSet<QString>& ids, bool ripple) {
    struct Range { double s, e; };
    QVector<Range> ranges;
    for (auto* tracks : {&tl.video, &tl.audio}) {
        for (Track& tr : *tracks) {
            if (tr.locked) continue;
            QVector<Clip> keep;
            for (const Clip& c : tr.clips) {
                if (ids.contains(c.id)) ranges.push_back({c.start, c.end()});
                else keep.push_back(c);
            }
            tr.clips = keep;
        }
    }
    if (!ripple || ranges.isEmpty()) return;
    std::sort(ranges.begin(), ranges.end(), [](const Range& a, const Range& b) { return a.s < b.s; });
    QVector<Range> merged;
    for (const Range& r : ranges) {
        if (!merged.isEmpty() && r.s <= merged.last().e + kEps) merged.last().e = std::max(merged.last().e, r.e);
        else merged.push_back(r);
    }
    for (int i = merged.size() - 1; i >= 0; --i) {
        const double len = merged[i].e - merged[i].s;
        for (auto* tracks : {&tl.video, &tl.audio})
            for (Track& tr : *tracks) {
                if (tr.locked) continue;
                for (Clip& c : tr.clips)
                    if (c.start >= merged[i].e - kEps) c.start -= len;
            }
    }
}

int cutRange(Timeline& tl, double s, double e, bool ripple) {
    if (e - s < kMinDur) return 0;
    splitAllAt(tl, e);
    splitAllAt(tl, s);
    QSet<QString> ids;
    for (const auto* tracks : {&tl.video, &tl.audio})
        for (const Track& tr : *tracks) {
            if (tr.locked) continue;
            for (const Clip& c : tr.clips)
                if (c.start >= s - kEps && c.end() <= e + kEps) ids.insert(c.id);
        }
    remove(tl, ids, ripple);
    return int(ids.size());
}

int cutRanges(Timeline& tl, QVector<TimeRange> ranges, bool ripple) {
    std::sort(ranges.begin(), ranges.end(), [](const TimeRange& a, const TimeRange& b) { return a.start > b.start; });
    int n = 0;
    for (const TimeRange& r : ranges) n += cutRange(tl, r.start, r.end, ripple);
    return n;
}

Timeline moved(const Timeline& base, const QSet<QString>& ids, double dt, int dTrack, TrackType primary) {
    Timeline tl = base;
    struct M { Clip c; TrackType type; int track; };
    QVector<M> list;
    double minStart = std::numeric_limits<double>::max();
    for (TrackType type : {TrackType::Video, TrackType::Audio}) {
        auto& tracks = tl.tracks(type);
        for (int t = 0; t < tracks.size(); ++t) {
            QVector<Clip> keep;
            for (const Clip& c : tracks[t].clips) {
                if (ids.contains(c.id) && !tracks[t].locked) {
                    list.push_back({c, type, t});
                    minStart = std::min(minStart, c.start);
                } else {
                    keep.push_back(c);
                }
            }
            tracks[t].clips = keep;
        }
    }
    if (list.isEmpty()) return base;
    if (minStart + dt < 0) dt = -minStart;

    // clamp the vertical move so every clip of the primary type stays on a valid track
    int lo = -1000, hi = 1000;
    for (const M& m : list) {
        if (m.type != primary) continue;
        const int n = tl.tracks(m.type).size();
        lo = std::max(lo, -m.track);
        hi = std::min(hi, n - 1 - m.track);
    }
    dTrack = std::max(lo, std::min(hi, dTrack));

    for (M& m : list) {
        const int nt = (m.type == primary) ? m.track + dTrack : m.track;
        Track& tr = tl.tracks(m.type)[nt];
        if (tr.locked) { tl = base; return base; }
        m.c.start = std::max(0.0, m.c.start + dt);
        place(tr, m.c);
    }
    return tl;
}

struct Bounds { double lo = -1e18, hi = 1e18; };

static void trackNeighbours(const Track& tr, const Clip& c, const QSet<QString>& ids, double& prevEnd, double& nextStart) {
    prevEnd = 0;
    nextStart = std::numeric_limits<double>::infinity();
    for (const Clip& o : tr.clips) {
        if (o.id == c.id || ids.contains(o.id)) continue;
        if (o.end() <= c.start + kEps) prevEnd = std::max(prevEnd, o.end());
        else if (o.start >= c.end() - kEps) nextStart = std::min(nextStart, o.start);
    }
}

double clampTrimDelta(const Timeline& tl, const QSet<QString>& ids, bool leftEdge, double delta, bool ripple) {
    Bounds b;
    for (const QString& id : ids) {
        const ClipRef r = tl.find(id);
        if (!r.valid()) continue;
        const Track& tr = tl.tracks(r.type)[r.track];
        const Clip& c = tr.clips[r.index];
        double prevEnd, nextStart;
        trackNeighbours(tr, c, ids, prevEnd, nextStart);
        if (leftEdge) {
            b.hi = std::min(b.hi, c.duration - kMinDur);                       // shrink limit
            if (!ripple) b.lo = std::max(b.lo, prevEnd - c.start);              // neighbour
            if (c.sourceLength > 0) b.lo = std::max(b.lo, -c.srcIn / c.speed);  // source start
        } else {
            b.lo = std::max(b.lo, -(c.duration - kMinDur));
            if (!ripple) b.hi = std::min(b.hi, nextStart - c.end());
            if (c.sourceLength > 0) b.hi = std::min(b.hi, (c.sourceLength - c.srcIn) / c.speed - c.duration);
        }
    }
    if (b.lo > b.hi) return 0;
    return std::max(b.lo, std::min(b.hi, delta));
}

double trim(Timeline& tl, const QSet<QString>& ids, bool leftEdge, double delta, bool ripple) {
    const double d = clampTrimDelta(tl, ids, leftEdge, delta, ripple);
    if (std::abs(d) < kEps) return 0;
    double oldEnd = 0;
    bool first = true;
    for (const QString& id : ids) {
        Clip* c = tl.clip(id);
        if (!c) continue;
        if (first) { oldEnd = c->end(); first = false; } else oldEnd = std::max(oldEnd, c->end());
    }
    for (const QString& id : ids) {
        Clip* c = tl.clip(id);
        if (!c) continue;
        if (leftEdge) {
            if (!ripple) c->start += d;
            c->srcIn += d * c->speed;
            c->duration -= d;
        } else {
            c->duration += d;
        }
    }
    if (ripple) {
        // later clips follow the change in length of the trimmed clips
        const double shift = leftEdge ? -d : d;
        for (auto* tracks : {&tl.video, &tl.audio})
            for (Track& tr : *tracks) {
                if (tr.locked) continue;
                for (Clip& c : tr.clips)
                    if (!ids.contains(c.id) && c.start >= oldEnd - kEps) c.start += shift;
            }
    }
    return d;
}

int setSpeed(Timeline& tl, const QSet<QString>& idsIn, double speed, bool ripple) {
    speed = std::max(0.05, std::min(speed, 20.0));
    const QSet<QString> ids = expandLinked(tl, idsIn);
    QVector<QString> work;   // only clips that play footage from a file have a speed
    for (const QString& id : ids) {
        const Clip* c = tl.clip(id);
        if (c && c->kind == ClipKind::Media && c->mediaKind != MediaKind::Image) work.push_back(id);
    }
    if (work.isEmpty()) return 0;
    // from the last clip to the first, so the ripple shifts of later clips stay valid
    std::sort(work.begin(), work.end(), [&](const QString& a, const QString& b) { return tl.clip(a)->start > tl.clip(b)->start; });
    QSet<QString> done;
    int changed = 0;
    for (const QString& id : work) {
        if (done.contains(id)) continue;
        QSet<QString> group;   // this clip and the clips linked to it (picture + sound change together)
        for (const QString& other : linkedIds(tl, id))
            if (work.contains(other)) group.insert(other);
        double oldEnd = 0, delta = 0;
        bool first = true;
        for (const QString& gid : group) {
            Clip* c = tl.clip(gid);
            oldEnd = std::max(oldEnd, c->end());
            const double newDur = c->duration * c->speed / speed;
            if (first) { delta = newDur - c->duration; first = false; }
            c->speed = speed;
            c->duration = newDur;
            done.insert(gid);
            ++changed;
        }
        if (ripple && std::abs(delta) > kEps) {
            for (auto* tracks : {&tl.video, &tl.audio})
                for (Track& tr : *tracks) {
                    if (tr.locked) continue;
                    for (Clip& c : tr.clips)
                        if (!group.contains(c.id) && c.start >= oldEnd - kEps) c.start += delta;
                }
        } else if (!ripple && delta > kEps) {
            for (const QString& gid : group) {          // a longer clip overwrites what follows on its own track
                const ClipRef r = tl.find(gid);
                const Clip copy = *tl.clip(r);
                place(tl.tracks(r.type)[r.track], copy);
            }
        }
    }
    return changed;
}

QVector<Clip> makeClips(const MediaItem& m, double srcIn, double dur, double start) {
    QVector<Clip> out;
    const bool videoPart = (m.kind == MediaKind::Video && m.hasVideo) || m.kind == MediaKind::Image;
    const bool audioPart = m.hasAudio && m.kind != MediaKind::Image;
    const QString link = (videoPart && audioPart) ? newId() : QString();

    auto base = [&](MediaKind k) {
        Clip c;
        c.id = newId();
        c.kind = ClipKind::Media;
        c.mediaId = m.id;
        c.path = m.path;
        c.mediaKind = k;
        c.name = m.name;
        c.linkId = link;
        c.start = start;
        c.srcIn = m.kind == MediaKind::Image ? 0 : srcIn;
        c.duration = dur;
        c.sourceLength = m.kind == MediaKind::Image ? 0 : m.duration;
        return c;
    };
    if (videoPart) {
        Clip c = base(m.kind == MediaKind::Image ? MediaKind::Image : MediaKind::Video);
        c.effects = {EffectRegistry::make("motion"), EffectRegistry::make("opacity")};
        out.push_back(c);
    }
    if (audioPart) {
        Clip c = base(MediaKind::Audio);
        c.effects = {EffectRegistry::make("volume")};
        out.push_back(c);
    }
    return out;
}

Clip makeTitle(double start, double dur, const QString& text) {
    Clip c;
    c.id = newId();
    c.kind = ClipKind::Title;
    c.mediaKind = MediaKind::Image;
    c.name = text.left(24);
    c.start = start;
    c.duration = dur;
    c.sourceLength = 0;
    c.effects = {EffectRegistry::make("text"), EffectRegistry::make("motion"), EffectRegistry::make("opacity")};
    c.effect("text")->set("text", text);
    return c;
}

void placeClips(Timeline& tl, const QVector<Clip>& clips, int vTrack, int aTrack, bool insert) {
    if (clips.isEmpty()) return;
    if (insert) insertSpace(tl, clips[0].start, clips[0].duration);
    for (const Clip& c : clips) {
        const bool isAudio = c.mediaKind == MediaKind::Audio && c.kind == ClipKind::Media;
        auto& tracks = isAudio ? tl.audio : tl.video;
        int idx = isAudio ? aTrack : vTrack;
        idx = std::max(0, std::min(idx, int(tracks.size()) - 1));
        if (tracks.isEmpty() || tracks[idx].locked) continue;
        place(tracks[idx], c);
    }
}

void addTrack(Timeline& tl, TrackType type) {
    Track t;
    t.type = type;
    auto& tracks = tl.tracks(type);
    t.name = QString(type == TrackType::Video ? "V%1" : "A%1").arg(tracks.size() + 1);
    tracks.push_back(t);
}

bool removeTrack(Timeline& tl, TrackType type, int index) {
    auto& tracks = tl.tracks(type);
    if (tracks.size() <= 1 || index < 0 || index >= tracks.size()) return false;
    tracks.removeAt(index);
    for (int i = 0; i < tracks.size(); ++i)
        tracks[i].name = QString(type == TrackType::Video ? "V%1" : "A%1").arg(i + 1);
    return true;
}

QVector<double> editPoints(const Timeline& tl, const QSet<QString>& exclude) {
    QVector<double> pts;
    pts << 0.0;
    for (const auto* tracks : {&tl.video, &tl.audio})
        for (const Track& tr : *tracks)
            for (const Clip& c : tr.clips) {
                if (exclude.contains(c.id)) continue;
                pts << c.start << c.end();
            }
    for (const Marker& m : tl.markers) pts << m.time;
    if (tl.markIn >= 0) pts << tl.markIn;
    if (tl.markOut >= 0) pts << tl.markOut;
    std::sort(pts.begin(), pts.end());
    pts.erase(std::unique(pts.begin(), pts.end(), [](double a, double b) { return std::abs(a - b) < kEps; }), pts.end());
    return pts;
}

double nextEdit(const Timeline& tl, double t) {
    for (double p : editPoints(tl))
        if (p > t + kEps) return p;
    return tl.duration();
}

double prevEdit(const Timeline& tl, double t) {
    double best = 0;
    for (double p : editPoints(tl))
        if (p < t - kEps) best = p;
    return best;
}

}  // namespace hr::ops
