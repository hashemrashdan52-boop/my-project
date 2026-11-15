#pragma once
#include "core/Model.h"

#include <QString>
#include <QVector>

namespace hr::ai {

// ---- subtitle files --------------------------------------------------------------------------
QVector<TranscriptSegment> parseSrt(const QString& text);
QString formatSrt(const QVector<TranscriptSegment>& segments);
QString cleanText(const QString& text);
// "[BLANK_AUDIO]", "(music)", "♪♪", "*laughs*": annotations, not speech
bool isNonSpeech(const QString& text);

// ---- a transcript as it sits on the timeline ---------------------------------------------------
struct TimelineSegment {
    double start = 0, end = 0;   // timeline seconds
    QString text;
    QString clipId;              // the audio clip it came from
    QString mediaId;
};

// Maps the transcript of every media file through the audio clips that use it (trimmed and moved clips are
// handled), sorted by time.
QVector<TimelineSegment> transcriptOnTimeline(const Timeline& tl, const QVector<MediaItem>& media);

// ---- captions ----------------------------------------------------------------------------------
// Splits long sentences into chunks of at most `maxChars` characters, sharing the time by length.
QVector<TimelineSegment> splitForCaptions(const QVector<TimelineSegment>& segments, int maxChars = 60);
// One title clip per segment.
QVector<Clip> makeCaptionClips(const QVector<TimelineSegment>& segments);
// Puts caption clips on the top-most free video track (adds a track when every track is busy there).
// Returns the index of the track that was used.
int addCaptions(Timeline& tl, const QVector<TimelineSegment>& segments);

// ---- text based editing ------------------------------------------------------------------------
// Merges overlapping / touching ranges.
QVector<TimeRange> mergeRanges(QVector<TimeRange> ranges, double gap = 0.0);
// Everything inside [0, total) that is NOT covered by `keep` (each kept range grown by `handle` seconds).
QVector<TimeRange> complementRanges(QVector<TimeRange> keep, double total, double handle = 0.1);

}  // namespace hr::ai
