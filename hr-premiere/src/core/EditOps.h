#pragma once
#include "Model.h"

#include <QVector>

// Pure editing algorithms on a Timeline.  No UI, no undo handling: callers take a
// copy first (see Project::edit) which keeps these functions trivial to unit test.
namespace hr::ops {

constexpr double kEps = 1e-6;

QSet<QString> linkedIds(const Timeline& tl, const QString& id);       // includes id
QSet<QString> expandLinked(const Timeline& tl, const QSet<QString>& ids);

// Removes [s,e) from a track (clips are trimmed / split / removed).
void carve(Track& track, double s, double e, const QSet<QString>& ignore = {});
// Overwrite-places a clip on a track (existing clips underneath are replaced).
void place(Track& track, const Clip& clip);
// Opens a gap of `dur` seconds at `at` on every unlocked track (Insert edit).
void insertSpace(Timeline& tl, double at, double dur);

// Splits the clip (and the clips linked to it) at time t.
bool splitAt(Timeline& tl, const QString& clipId, double t);
// Splits every unlocked track at t ("Add Edit to All Tracks"). Returns #clips split.
int splitAllAt(Timeline& tl, double t);

// Removes the time range [s,e) from every unlocked track (splitting clips at both ends, keeping linked
// pairs together) and, with ripple, closes the gap.  Returns the number of clips removed.
int cutRange(Timeline& tl, double s, double e, bool ripple = true);
// Several ranges at once; they are processed from the last to the first so earlier times stay valid.
int cutRanges(Timeline& tl, QVector<TimeRange> ranges, bool ripple = true);

// Deletes clips; ripple closes the gaps on every track.
void remove(Timeline& tl, const QSet<QString>& ids, bool ripple);

// Returns a copy of `base` in which the clips were moved by dt seconds and dTrack
// tracks (only clips on tracks of `primary` type change track).  Overwrites whatever
// the clips land on.  dt is clamped so nothing starts before 0.
Timeline moved(const Timeline& base, const QSet<QString>& ids, double dt, int dTrack, TrackType primary);

// Trim: delta > 0 moves the edge to the right (later in time).  Returns the delta
// actually applied (clamped by neighbours, source length and min duration).
double clampTrimDelta(const Timeline& tl, const QSet<QString>& ids, bool leftEdge, double delta, bool ripple = false);
double trim(Timeline& tl, const QSet<QString>& ids, bool leftEdge, double delta, bool ripple = false);

// Changes the speed of footage clips (picture and linked sound together): 1.0 = normal, 2.0 = twice as fast.
// With ripple the clips behind move to follow the new length, otherwise a longer clip overwrites them.
// Returns the number of clips changed.
int setSpeed(Timeline& tl, const QSet<QString>& ids, double speed, bool ripple);

// Clips (video + linked audio) for a media item, ready to be placed at `start`.
QVector<Clip> makeClips(const MediaItem& m, double srcIn, double dur, double start);
Clip makeTitle(double start, double dur, const QString& text);
// Places clips made by makeClips: video part on V[vTrack], audio part on A[aTrack].
// insert=true opens a gap first (Insert), otherwise overwrites (Overwrite / drag & drop).
void placeClips(Timeline& tl, const QVector<Clip>& clips, int vTrack, int aTrack, bool insert);

void addTrack(Timeline& tl, TrackType type);
bool removeTrack(Timeline& tl, TrackType type, int index);

// Editing points (clip starts / ends, markers, in/out) – used for snapping and Up/Down arrows.
QVector<double> editPoints(const Timeline& tl, const QSet<QString>& exclude = {});
double nextEdit(const Timeline& tl, double t);
double prevEdit(const Timeline& tl, double t);

}  // namespace hr::ops
