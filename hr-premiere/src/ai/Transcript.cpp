#include "Transcript.h"

#include "core/EditOps.h"
#include "core/TextUtil.h"
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
            for (int si = 0; si < m->transcript.size(); ++si) {
                const TranscriptSegment& s = m->transcript[si];
                const double a = std::max(s.start, in), b = std::min(s.end, outT);
                if (b - a < 0.05) continue;
                TimelineSegment ts;
                ts.index = si;
                ts.edited = s.edited;
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

QVector<Clip> makeCaptionClips(const QVector<TimelineSegment>& segments, const CaptionStyle& style) {
    QVector<Clip> out;
    for (const TimelineSegment& s : segments) {
        Clip c = ops::makeTitle(s.start, std::max(0.3, s.end - s.start), s.text);
        c.role = "caption";
        EffectInstance* tx = c.effect("text");
        tx->set("size", style.size);
        tx->set("bold", style.bold);
        tx->set("shadow", style.shadow);
        tx->set("color", style.color);
        c.effect("motion")->set("position_y", style.positionY);
        out.push_back(c);
    }
    return out;
}

int addCaptions(Timeline& tl, const QVector<TimelineSegment>& segments, const CaptionStyle& style) {
    const QVector<Clip> clips = makeCaptionClips(segments, style);
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

int removeCaptions(Timeline& tl) {
    int n = 0;
    for (Track& tr : tl.video) {
        if (tr.locked) continue;
        for (int i = int(tr.clips.size()) - 1; i >= 0; --i)
            if (tr.clips[i].role == "caption") { tr.clips.removeAt(i); ++n; }
    }
    return n;
}

int restyleCaptions(Timeline& tl, const QString& color, double size, double positionY) {
    int n = 0;
    for (Track& tr : tl.video)
        for (Clip& c : tr.clips) {
            if (c.role != "caption") continue;
            if (EffectInstance* tx = c.effect("text")) {
                if (!color.isEmpty()) tx->set("color", color);
                if (size > 0) tx->set("size", size);
            }
            if (!std::isnan(positionY))
                if (EffectInstance* mo = c.effect("motion")) mo->set("position_y", positionY);
            ++n;
        }
    return n;
}

int retextCaptions(Timeline& tl, double start, double end, const QString& text, int maxChars) {
    // the caption pieces whose middle lies inside the sentence
    int track = -1;
    Clip proto;
    QVector<QString> ids;
    for (int ti = 0; ti < tl.video.size(); ++ti)
        for (const Clip& c : tl.video[ti].clips) {
            const double mid = (c.start + c.end()) / 2.0;
            if (c.role != "caption" || mid < start - 1e-6 || mid > end + 1e-6) continue;
            if (track < 0) { track = ti; proto = c; }
            if (ti == track) ids.push_back(c.id);
        }
    if (ids.isEmpty()) return 0;
    double a = 1e18, b = 0;
    for (const Clip& c : tl.video[track].clips)
        if (ids.contains(c.id)) { a = std::min(a, c.start); b = std::max(b, c.end()); }
    Track& tr = tl.video[track];
    for (int i = int(tr.clips.size()) - 1; i >= 0; --i)
        if (ids.contains(tr.clips[i].id)) tr.clips.removeAt(i);
    TimelineSegment whole;
    whole.start = a;
    whole.end = b;
    whole.text = text;
    int made = 0;
    for (const TimelineSegment& part : splitForCaptions({whole}, maxChars)) {
        Clip c = proto;                      // same font, colour, size and position as before
        c.id = newId();
        c.start = part.start;
        c.duration = std::max(0.3, part.end - part.start);
        c.name = part.text.left(24);
        if (EffectInstance* tx = c.effect("text")) tx->set("text", part.text);
        ops::place(tr, c);
        ++made;
    }
    return made;
}

int replaceInCaptions(Timeline& tl, const QString& from, const QString& to) {
    if (from.trimmed().isEmpty()) return 0;
    const QRegularExpression re = tolerantRegex(from);
    QString safeTo = to;
    safeTo.replace('\\', QStringLiteral("\\\\"));
    int n = 0;
    for (Track& tr : tl.video)
        for (Clip& c : tr.clips) {
            if (c.role != "caption") continue;
            EffectInstance* tx = c.effect("text");
            if (!tx) continue;
            QString t = tx->text("text");
            int hits = 0;
            auto it = re.globalMatch(t);
            while (it.hasNext()) { it.next(); ++hits; }
            if (!hits) continue;
            t.replace(re, safeTo);
            tx->set("text", t);
            c.name = t.left(24);
            n += hits;
        }
    return n;
}

// ---- reliability ------------------------------------------------------------------------------------------
static double byteToDb(double v) { return 20.0 * std::log10(std::max(1.0, v) / 255.0); }

// phrases that speech models are known to invent on noise or silence (compared after normalizeArabic)
static bool looksInvented(const QString& normalized) {
    static const QStringList phrases = {
        "thanks for watching", "thank you for watching", "thank you very much for watching", "please subscribe",
        "like and subscribe", "subtitles by", "subtitles made by", "amara org", "transcribed by", "translated by",
        QString::fromUtf8("شكرا للمشاهده"), QString::fromUtf8("شكرا لمشاهدتكم"), QString::fromUtf8("شكرا لكم على المشاهده"),
        QString::fromUtf8("اشتركوا في القناه"), QString::fromUtf8("اشترك في القناه"), QString::fromUtf8("لا تنسوا الاشتراك"),
        QString::fromUtf8("ترجمه نانسي قنقر"), QString::fromUtf8("ترجمه المجتمع"), QString::fromUtf8("الي اللقاء في الحلقه القادمه"),
    };
    static const QStringList labels = {"music", QString::fromUtf8("موسيقي"), "applause", QString::fromUtf8("تصفيق"), "you", "bye", "."};
    const QString t = normalized.simplified();
    for (const QString& l : labels)
        if (t == l) return true;
    for (const QString& p : phrases)
        if (t.contains(p)) return true;
    return false;
}

QVector<SegmentQuality> assessTranscript(const QVector<TranscriptSegment>& segments, const QVector<quint8>* peaks100) {
    QVector<SegmentQuality> out(segments.size());
    const bool haveSound = peaks100 && peaks100->size() > 200;
    double noiseDb = -60;
    if (haveSound) {
        std::vector<quint8> v(peaks100->begin(), peaks100->end());
        std::nth_element(v.begin(), v.begin() + std::ptrdiff_t(v.size() * 15 / 100), v.end());
        noiseDb = byteToDb(v[v.size() * 15 / 100]);
    }
    QString previous;
    for (int i = 0; i < segments.size(); ++i) {
        const TranscriptSegment& sg = segments[i];
        SegmentQuality q;
        const QString norm = normalizeArabic(sg.text).simplified();
        const double dur = std::max(0.01, sg.end - sg.start);
        if (haveSound) {
            std::vector<quint8> v;
            const int a = std::max(0, int(sg.start * 100)), b = std::min(int(peaks100->size()), int(std::ceil(sg.end * 100)));
            for (int k = a; k < b; ++k) v.push_back((*peaks100)[k]);
            if (!v.empty()) {
                const size_t k = size_t(double(v.size() - 1) * 0.85 + 0.5);
                std::nth_element(v.begin(), v.begin() + std::ptrdiff_t(k), v.end());
                q.levelDb = byteToDb(v[k]);
                q.snrDb = q.levelDb - noiseDb;
            }
        }
        auto raise = [&](Quality to, const QString& why) {
            if (to > q.quality) q.quality = to;
            if (!q.reasons.contains(why)) q.reasons << why;
        };
        if (!sg.edited) {
            if (haveSound && q.levelDb != 0) {
                if (q.snrDb < 8) raise(Quality::Poor, "noisy");
                else if (q.snrDb < 15) raise(Quality::Doubtful, "noisy");
                if (q.levelDb < -34) raise(Quality::Doubtful, "quiet");
            }
            if (looksInvented(norm) && (!haveSound || q.snrDb < 18 || q.levelDb < -30)) raise(Quality::Poor, "hallucination");
            // the same word over and over
            const QStringList words = norm.split(' ', Qt::SkipEmptyParts);
            int run = 1, longest = 1;
            for (int w = 1; w < words.size(); ++w) {
                run = words[w] == words[w - 1] ? run + 1 : 1;
                longest = std::max(longest, run);
            }
            if (longest >= 4) raise(Quality::Poor, "repeated");
            else if (norm.size() >= 4 && norm == previous) raise(Quality::Doubtful, "repeated");
            // impossible speaking speed
            QString letters = norm;
            letters.remove(' ');
            const double cps = double(letters.size()) / dur;
            if (cps > 24) raise(Quality::Doubtful, "too-fast");
            if (dur >= 2.5 && cps < 1.0) raise(Quality::Doubtful, "too-slow");
            if (letters.size() <= 2 && haveSound && q.levelDb > -120 && q.levelDb < -25) raise(Quality::Doubtful, "tiny");
        }
        previous = norm;
        out[i] = q;
    }
    return out;
}

QVector<TranscriptSegment> sanitizeTranscript(const QVector<TranscriptSegment>& segments, const QVector<quint8>* peaks100, int* removed) {
    const QVector<SegmentQuality> q = assessTranscript(segments, peaks100);
    QVector<TranscriptSegment> out;
    int dropped = 0;
    QString previous;
    int repeats = 0;
    for (int i = 0; i < segments.size(); ++i) {
        const QString norm = normalizeArabic(segments[i].text).simplified();
        repeats = (norm.size() >= 2 && norm == previous) ? repeats + 1 : 0;
        previous = norm;
        const bool invented = q[i].reasons.contains("hallucination") && !segments[i].edited;
        if (invented || repeats >= 2 || norm.isEmpty()) { ++dropped; continue; }   // 3rd identical sentence in a row = a loop
        out.push_back(segments[i]);
    }
    if (removed) *removed = dropped;
    return out;
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
