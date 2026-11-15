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
    int index = -1;              // position of the sentence in that media file's transcript
    bool edited = false;         // corrected by the user
};

// Maps the transcript of every media file through the audio clips that use it (trimmed and moved clips are
// handled), sorted by time.
QVector<TimelineSegment> transcriptOnTimeline(const Timeline& tl, const QVector<MediaItem>& media);

// ---- captions ----------------------------------------------------------------------------------
// Splits long sentences into chunks of at most `maxChars` characters, sharing the time by length.
QVector<TimelineSegment> splitForCaptions(const QVector<TimelineSegment>& segments, int maxChars = 60);
struct CaptionStyle {
    double size = 54;            // px, for a 1080 pixel high picture
    double positionY = 36;       // % of the picture height below the centre (36 = lower third, negative = above)
    QString color = "#ffffff";
    bool bold = true;
    bool shadow = true;
};
// One title clip per segment (marked as role "caption").
QVector<Clip> makeCaptionClips(const QVector<TimelineSegment>& segments, const CaptionStyle& style = CaptionStyle());
// Puts caption clips on the top-most free video track (adds a track when every track is busy there).
// Returns the index of the track that was used.
int addCaptions(Timeline& tl, const QVector<TimelineSegment>& segments, const CaptionStyle& style = CaptionStyle());
// Removes every caption made by the assistant. Returns how many clips were removed.
int removeCaptions(Timeline& tl);
// Changes colour / size / position of the existing captions (only the values that are set). Returns the count.
int restyleCaptions(Timeline& tl, const QString& color, double size, double positionY);
// Replaces the captions of one sentence (found by time) with captions of `text`, keeping their look.
// Returns the number of new caption clips (0 when that sentence had no captions).
int retextCaptions(Timeline& tl, double start, double end, const QString& text, int maxChars = 56);
// Replaces a word in every caption (spelling variants are matched like in the transcript). Returns the count.
int replaceInCaptions(Timeline& tl, const QString& from, const QString& to);

// ---- how reliable is the recognised text? ----------------------------------------------------------------
enum class Quality { Good, Doubtful, Poor };
struct SegmentQuality {
    Quality quality = Quality::Good;
    QStringList reasons;         // "noisy", "quiet", "hallucination", "repeated", "too-fast", "too-slow", "tiny"
    double levelDb = 0;          // how loud that sentence was
    double snrDb = 0;            // ... above the background noise of the file
};
// An estimate, not a measurement of the recogniser's own confidence: poor sound (close to the noise floor), phrases
// that speech models invent on noise, loops, and impossible speaking speeds are flagged. Sentences the user has
// corrected are always Good.  `peaks100` = one byte per 10 ms of the media file (may be null / empty).
QVector<SegmentQuality> assessTranscript(const QVector<TranscriptSegment>& segments, const QVector<quint8>* peaks100);
// Drops what the model made up (invented sign-offs on noise, endless repeats) before the text is stored.
QVector<TranscriptSegment> sanitizeTranscript(const QVector<TranscriptSegment>& segments, const QVector<quint8>* peaks100, int* removed = nullptr);

// ---- text based editing ------------------------------------------------------------------------
// Merges overlapping / touching ranges.
QVector<TimeRange> mergeRanges(QVector<TimeRange> ranges, double gap = 0.0);
// Everything inside [0, total) that is NOT covered by `keep` (each kept range grown by `handle` seconds).
QVector<TimeRange> complementRanges(QVector<TimeRange> keep, double total, double handle = 0.1);

}  // namespace hr::ai
