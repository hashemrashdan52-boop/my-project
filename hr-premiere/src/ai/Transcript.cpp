#include "Transcript.h"

#include "core/EditOps.h"
#include "core/Util.h"

#include <QRegularExpression>
#include <QStringList>
#include <algorithm>
#include <cmath>

namespace hr::ai {

QString cleanText(const QString& text) {
    QString t = text;
    t.replace(QRegularExpression("\\s+"), " ");
    return t.trimmed();
}

bool isNonSpeech(const QString& text) {
    const QString t = cleanText(text);
    if (t.isEmpty()) return true;
    if (t.size() > 48) return false;
    static const QRegularExpression note(R"(^[\[\(\*♪♫\s].*[\]\)\*♪♫\s]$)");
    if (note.match(t).hasMatch()) return true;
    QString letters = t;
    letters.remove(QRegularExpression(R"([\s♪♫\*\.\-–—_])"));
    return letters.isEmpty();
}

static double parseStamp(const QRegularExpressionMatch& m, int first) {
    return m.captured(first).toDouble() * 3600.0 + m.captured(first + 1).toDouble() * 60.0 +
           m.captured(first + 2).toDouble() + m.captured(first + 3).leftJustified(3, '0').left(3).toDouble() / 1000.0;
}

QVector<TranscriptSegment> parseSrt(const QString& text) {
    static const QRegularExpression timeLine(
        R"((\d+):(\d+):(\d+)[,.](\d+)\s*-->\s*(\d+):(\d+):(\d+)[,.](\d+))");
    QVector<TranscriptSegment> out;
    const QStringList lines = QString(text).replace("\r\n", "\n").replace('\r', '\n').split('\n');
    int i = 0;
    while (i < lines.size()) {
        const auto m = timeLine.match(lines[i]);
        if (!m.hasMatch()) { ++i; continue; }
        TranscriptSegment seg;
        seg.start = parseStamp(m, 1);
        seg.end = parseStamp(m, 5);
        ++i;
        QStringList body;
        while (i < lines.size() && !lines[i].trimmed().isEmpty()) {
            if (timeLine.match(lines[i]).hasMatch()) break;
            body << lines[i];
            ++i;
        }
        // a numeric cue index that belongs to the next block may have been swallowed
        if (!body.isEmpty() && QRegularExpression("^\\d+$").match(body.last().trimmed()).hasMatch() && i < lines.size() &&
            timeLine.match(lines[i]).hasMatch())
            body.removeLast();
        seg.text = cleanText(body.join(' '));
        if (seg.end > seg.start && !isNonSpeech(seg.text)) out.push_back(seg);
    }
    return out;
}

static QString stamp(double t) {
    t = std::max(0.0, t);
    const qint64 ms = qint64(std::llround(t * 1000.0));
    return QString("%1:%2:%3,%4")
        .arg(ms / 3600000, 2, 10, QLatin1Char('0'))
        .arg(ms / 60000 % 60, 2, 10, QLatin1Char('0'))
        .arg(ms / 1000 % 60, 2, 10, QLatin1Char('0'))
        .arg(ms % 1000, 3, 10, QLatin1Char('0'));
}

QString formatSrt(const QVector<TranscriptSegment>& segments) {
    QString out;
    int n = 1;
    for (const TranscriptSegment& s : segments)
        out += QString("%1\n%2 --> %3\n%4\n\n").arg(n++).arg(stamp(s.start), stamp(s.end), s.text);
    return out;
}

QVector<TimelineSegment> transcriptOnTimeline(const Timeline& tl, const QVector<MediaItem>& media) {
    QHash<QString, const MediaItem*> byId;
    for (const MediaItem& m : media) byId.insert(m.id, &m);
    QVector<TimelineSegment> out;
    for (const Track& tr : tl.audio) {
        if (!tr.enabled) continue;
        for (const Clip& c : tr.clips) {
            if (!c.enabled || c.kind != ClipKind::Media || c.mediaId.isEmpty()) continue;
            const MediaItem* m = byId.value(c.mediaId, nullptr);
            if (!m || m->transcript.isEmpty()) continue;
            const double in = c.srcIn, outT = c.srcIn + c.duration * c.speed;
            for (const TranscriptSegment& s : m->transcript) {
                const double a = std::max(s.start, in), b = std::min(s.end, outT);
                if (b - a < 0.05) continue;
                TimelineSegment ts;
                ts.start = c.start + (a - in) / c.speed;
                ts.end = c.start + (b - in) / c.speed;
                ts.text = s.text;
                ts.clipId = c.id;
                ts.mediaId = c.mediaId;
                out.push_back(ts);
            }
        }
    }
    std::sort(out.begin(), out.end(), [](const TimelineSegment& a, const TimelineSegment& b) { return a.start < b.start; });
    return out;
}

QVector<TimelineSegment> splitForCaptions(const QVector<TimelineSegment>& segments, int maxChars) {
    QVector<TimelineSegment> out;
    for (const TimelineSegment& s : segments) {
        const QStringList words = s.text.split(' ', Qt::SkipEmptyParts);
        if (s.text.size() <= maxChars || words.size() < 2) { out.push_back(s); continue; }
        // greedy chunks of whole words
        QVector<QString> chunks;
        QString cur;
        for (const QString& w : words) {
            if (!cur.isEmpty() && cur.size() + 1 + w.size() > maxChars) { chunks.push_back(cur); cur.clear(); }
            cur += (cur.isEmpty() ? "" : " ") + w;
        }
        if (!cur.isEmpty()) chunks.push_back(cur);
        int total = 0;
        for (const QString& c : chunks) total += c.size();
        double t = s.start;
        for (const QString& c : chunks) {
            TimelineSegment piece = s;
            piece.text = c;
            piece.start = t;
            t += (s.end - s.start) * double(c.size()) / double(std::max(1, total));
            piece.end = t;
            out.push_back(piece);
        }
    }
    return out;
}

QVector<Clip> makeCaptionClips(const QVector<TimelineSegment>& segments) {
    QVector<Clip> out;
    for (const TimelineSegment& s : segments) {
        Clip c = ops::makeTitle(s.start, std::max(0.3, s.end - s.start), s.text);
        EffectInstance* tx = c.effect("text");
        tx->set("size", 54.0);
        tx->set("bold", true);
        tx->set("shadow", true);
        c.effect("motion")->set("position_y", 36.0);     // lower third of the picture
        out.push_back(c);
    }
    return out;
}

int addCaptions(Timeline& tl, const QVector<TimelineSegment>& segments) {
    const QVector<Clip> clips = makeCaptionClips(segments);
    if (clips.isEmpty()) return -1;
    const double a = clips.first().start, b = clips.last().end();
    // the top-most track with nothing in the caption range
    int track = -1;
    for (int i = int(tl.video.size()) - 1; i >= 0 && track < 0; --i) {
        bool busy = false;
        for (const Clip& c : tl.video[i].clips)
            if (c.end() > a + 1e-6 && c.start < b - 1e-6) busy = true;
        if (!busy && !tl.video[i].locked) track = i;
    }
    if (track < 0) {
        ops::addTrack(tl, TrackType::Video);
        track = int(tl.video.size()) - 1;
    }
    for (const Clip& c : clips) ops::place(tl.video[track], c);
    return track;
}

QVector<TimeRange> mergeRanges(QVector<TimeRange> ranges, double gap) {
    std::sort(ranges.begin(), ranges.end(), [](const TimeRange& a, const TimeRange& b) { return a.start < b.start; });
    QVector<TimeRange> out;
    for (const TimeRange& r : ranges) {
        if (r.end <= r.start) continue;
        if (!out.isEmpty() && r.start <= out.last().end + gap) out.last().end = std::max(out.last().end, r.end);
        else out.push_back(r);
    }
    return out;
}

QVector<TimeRange> complementRanges(QVector<TimeRange> keep, double total, double handle) {
    for (TimeRange& r : keep) {
        r.start = std::max(0.0, r.start - handle);
        r.end = std::min(total, r.end + handle);
    }
    const QVector<TimeRange> merged = mergeRanges(keep, 0.0);
    QVector<TimeRange> out;
    double pos = 0;
    for (const TimeRange& r : merged) {
        if (r.start - pos > 1e-3) out.push_back({pos, r.start});
        pos = std::max(pos, r.end);
    }
    if (total - pos > 1e-3) out.push_back({pos, total});
    return out;
}

}  // namespace hr::ai
