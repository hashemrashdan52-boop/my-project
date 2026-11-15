#include "SmartEdit.h"

#include "core/EditOps.h"
#include "core/Util.h"

#include <QHash>
#include <algorithm>
#include <cmath>

namespace hr::ai {

static bool isNum(double v) { return !std::isnan(v); }

// ------------------------------------------------------------------------------------------------ analysis
int mainVideoTrack(const Timeline& tl) {
    int best = -1;
    double bestLen = 0;
    for (int i = 0; i < tl.video.size(); ++i) {
        double len = 0;
        for (const Clip& c : tl.video[i].clips)
            if (c.kind == ClipKind::Media && c.enabled) len += c.duration;
        if (len > bestLen + 1e-9) { bestLen = len; best = i; }
    }
    return best;
}

VideoProfile analyzeProject(const Timeline& tl, const QVector<MediaItem>& media, const PeaksProvider& peaks) {
    VideoProfile p;
    p.duration = tl.duration();
    const int mt = mainVideoTrack(tl);
    if (mt >= 0) {
        p.hasVideo = true;
        for (const Clip& c : tl.video[mt].clips)
            if (c.kind == ClipKind::Media) ++p.shots;
    }
    p.vertical = tl.height > tl.width;
    p.audio = measureAudio(buildLevelMap(tl, peaks));
    const QVector<TimelineSegment> segs = transcriptOnTimeline(tl, media);
    p.hasTranscript = !segs.isEmpty();
    p.sentences = int(segs.size());
    if (p.hasTranscript && p.duration > 0) {
        QVector<TimeRange> spans;
        int words = 0;
        for (const TimelineSegment& s : segs) {
            spans.push_back({s.start, s.end});
            words += int(s.text.split(' ', Qt::SkipEmptyParts).size());
        }
        double talk = 0;
        for (const TimeRange& r : mergeRanges(spans, 0.0)) talk += r.length();
        p.speechRatio = std::min(1.0, talk / p.duration);
        p.wordsPerSecond = talk > 0.5 ? double(words) / talk : 0;
    }
    return p;
}

// ------------------------------------------------------------------------------------------------ the plan
EditPlan planEdit(const VideoProfile& pr, const EditStyle& st) {
    EditPlan pl;
    pl.style = st;
    const bool haveAudio = pr.audio.valid;
    pl.speechDb = pr.audio.speechDb;
    pl.noiseDb = pr.audio.noiseFloorDb;
    const bool noisy = haveAudio && pr.audio.dynamicDb() < 21;     // speech hardly louder than the background
    const bool quiet = haveAudio && pr.audio.speechDb < -24;
    const bool talking = pr.hasTranscript && pr.speechRatio >= 0.25;
    const double wps = pr.wordsPerSecond;

    // ---- dead air
    double thr = -40;
    if (isNum(st.thresholdDb)) thr = st.thresholdDb;
    else if (haveAudio) thr = std::clamp(pr.audio.noiseFloorDb + 0.3 * pr.audio.dynamicDb() + 3.0, -52.0, -30.0);
    double minSilence = wps >= 3.2 ? 0.35 : (wps >= 2.2 ? 0.45 : 0.6);
    minSilence *= st.pace == Pace::Fast ? 0.8 : (st.pace == Pace::Calm ? 1.4 : 1.0);
    if (isNum(st.minSilence)) minSilence = st.minSilence;
    double padding = st.pace == Pace::Fast ? 0.10 : (st.pace == Pace::Calm ? 0.20 : 0.14);
    if (isNum(st.padding)) padding = std::clamp(st.padding, 0.0, 1.0);
    pl.silence.thresholdDb = thr;
    pl.silence.minSilence = std::clamp(minSilence, 0.2, 3.0);
    pl.silence.padding = padding;
    // In strong background noise the waveform cannot tell a pause from speech, so it is only trusted when the user
    // gave a threshold; the recognised sentences find the pauses instead (and with no sentences nothing is guessed).
    pl.cutByWaveform = !noisy || isNum(st.thresholdDb);
    pl.cutSpeechGaps = pr.hasTranscript && (!haveAudio || pr.audio.dynamicDb() >= 14);

    if (talking) pl.reasons << "talking";
    if (wps >= 3.2) pl.reasons << "fast-talker";
    else if (wps > 0 && wps < 2.2) pl.reasons << "slow-talker";
    if (noisy) pl.reasons << "noisy";
    else if (quiet) pl.reasons << "quiet";
    if (!pr.hasTranscript) pl.reasons << "no-speech";
    if (pr.vertical) pl.reasons << "vertical";

    // ---- movement
    const double strength = std::clamp(st.motionStrength, 0.0, 2.0);
    switch (st.pace) {
    case Pace::Fast: pl.maxShot = 4.5; pl.punch = true; pl.punchPercent = 14 * strength; pl.pushPercent = 5 * strength; pl.reasons << "fast"; break;
    case Pace::Calm: pl.maxShot = 11; pl.punch = false; pl.punchPercent = 0; pl.pushPercent = 5 * strength; pl.reasons << "calm"; break;
    default: pl.maxShot = 7; pl.punch = true; pl.punchPercent = 11 * strength; pl.pushPercent = 4 * strength; break;
    }
    if (!st.motion || strength <= 0) pl.punchPercent = pl.pushPercent = 0;

    // ---- fades and captions
    pl.fadeIn = st.fades ? 0.4 : 0;
    pl.fadeOut = st.fades ? 0.6 : 0;
    pl.captionChars = pr.vertical ? 26 : 48;
    pl.caption.size = st.captionSize > 0 ? st.captionSize : (pr.vertical ? 34 : 54);
    pl.caption.positionY = isNum(st.captionY) ? st.captionY : (pr.vertical ? 30 : 36);
    if (!st.captionColor.isEmpty()) pl.caption.color = st.captionColor;
    return pl;
}

// ------------------------------------------------------------------------------------------------ applying it
// Gaps between recognised sentences that hold no real sound are dead air too, even when background noise keeps the
// waveform from looking silent.  A gap with strong sound but no words is kept: the model may have missed speech.
static QVector<TimeRange> speechGapCuts(const Timeline& tl, const QVector<MediaItem>& media, const LevelMap& map, const EditPlan& plan,
                                        int* keptLoud) {
    QVector<TimeRange> out;
    const QVector<TimelineSegment> segs = transcriptOnTimeline(tl, media);
    const double minGap = std::max(plan.silence.minSilence, 0.5);
    const double pad = plan.silence.padding + 0.05;
    for (int i = 0; i + 1 < segs.size(); ++i) {
        const double a = segs[i].end, b = segs[i + 1].start;
        if (b - a < minGap) continue;
        double s = a + pad, e = b - pad;
        if (e - s < 0.15) continue;
        const double loud = levelPercentileDb(map, a, b, 0.9);
        if (loud <= -119) continue;                                  // no sound plays there
        if (loud < plan.speechDb - 14.0) {
            s = std::ceil(s * tl.fps - 1e-6) / tl.fps;
            e = std::floor(e * tl.fps + 1e-6) / tl.fps;
            if (e - s >= 2.0 / tl.fps) out.push_back({s, e});
        } else if (keptLoud) {
            ++*keptLoud;
        }
    }
    return out;
}

// Long takes are split where a sentence starts, so the picture can change (zoom) now and then.
static int splitLongShots(Timeline& tl, const EditPlan& plan, const QVector<MediaItem>& media) {
    if (!plan.style.motion || (plan.punchPercent <= 0 && plan.pushPercent <= 0)) return 0;
    const int mt = mainVideoTrack(tl);
    if (mt < 0) return 0;
    const QVector<TimelineSegment> segs = transcriptOnTimeline(tl, media);
    if (segs.isEmpty()) return 0;
    QVector<double> splits;
    for (const Clip& c : tl.video[mt].clips) {
        if (c.kind != ClipKind::Media || c.duration <= plan.maxShot * 1.25) continue;
        double last = c.start;
        for (double target = c.start + plan.maxShot; target < c.end() - 1.5; target += plan.maxShot) {
            double best = -1, bestD = plan.maxShot * 0.45;
            for (const TimelineSegment& s : segs) {
                if (s.start <= last + 1.5 || s.start >= c.end() - 1.5) continue;
                const double d = std::abs(s.start - target);
                if (d < bestD) { bestD = d; best = s.start; }
            }
            if (best < 0) continue;
            best = snapToFrame(std::max(c.start + 0.1, best - 0.05), tl.fps);   // a hair before the first word
            splits.push_back(best);
            last = best;
            target = best;
        }
    }
    for (double t : splits) ops::splitAllAt(tl, t);
    return int(splits.size());
}

int clearMotion(Timeline& tl) {
    int n = 0;
    for (Track& tr : tl.video)
        for (Clip& c : tr.clips) {
            if (c.role != "motion") continue;
            if (EffectInstance* mo = c.effect("motion")) {
                mo->set("scale", 100.0);
                mo->set("zoom_amount", 0.0);
                mo->set("pan_x", 0.0);
                mo->set("pan_y", 0.0);
            }
            c.role.clear();
            ++n;
        }
    return n;
}

int applyMotion(Timeline& tl, const EditPlan& plan) {
    clearMotion(tl);
    if (!plan.style.motion || (plan.punchPercent <= 0 && plan.pushPercent <= 0)) return 0;
    const int mt = mainVideoTrack(tl);
    if (mt < 0) return 0;
    int changed = 0, index = 0;
    for (Clip& c : tl.video[mt].clips) {
        if (c.kind != ClipKind::Media || c.mediaKind == MediaKind::Audio || !c.enabled) continue;
        EffectInstance* mo = c.effect("motion");
        if (!mo) continue;
        const int i = index++;
        double base = 100, zoom = 0;
        if (plan.punch) {
            if (i % 2 == 1) base = 100 + plan.punchPercent * (i % 4 == 3 ? 1.5 : 1.0);     // every other shot is closer
            if (c.duration >= 1.5) zoom = plan.pushPercent;                                  // and all of them creep in slowly
        } else if (c.duration >= 1.5) {                                                      // calm: breathing in and out
            if (i % 2 == 0) {
                zoom = plan.pushPercent;
            } else {
                base = 100 + plan.pushPercent;
                zoom = (100.0 / base - 1.0) * 100.0;
            }
        }
        const double end = base * (1 + zoom / 100.0);
        if (std::max(base, end) > 150.0) {                                                  // never magnify more than 150 %
            const double cb = std::min(base, 150.0), ce = std::min(end, 150.0);
            base = cb;
            zoom = (ce / cb - 1.0) * 100.0;
        }
        mo->set("scale", base);
        mo->set("zoom_amount", zoom);
        if (std::abs(base - 100) > 1e-9 || std::abs(zoom) > 1e-9) {
            c.role = "motion";
            ++changed;
        }
    }
    return changed;
}

void clearFades(Timeline& tl) {
    for (Track& tr : tl.video)
        for (Clip& c : tr.clips)
            if (c.kind == ClipKind::Media)
                if (EffectInstance* op = c.effect("opacity")) { op->set("fade_in", 0.0); op->set("fade_out", 0.0); }
    for (Track& tr : tl.audio)
        for (Clip& c : tr.clips)
            if (EffectInstance* v = c.effect("volume")) { v->set("fade_in", 0.0); v->set("fade_out", 0.0); }
}

void applyFades(Timeline& tl, double fadeIn, double fadeOut) {
    clearFades(tl);
    // picture: the first clip fades in, the last fades out
    const int mt = mainVideoTrack(tl);
    if (mt >= 0) {
        Clip* first = nullptr;
        Clip* last = nullptr;
        for (Clip& c : tl.video[mt].clips) {
            if (c.kind != ClipKind::Media) continue;
            if (!first) first = &c;
            last = &c;
        }
        if (first && fadeIn > 0)
            if (EffectInstance* op = first->effect("opacity")) op->set("fade_in", std::min(fadeIn, first->duration / 3.0));
        if (last && fadeOut > 0)
            if (EffectInstance* op = last->effect("opacity")) op->set("fade_out", std::min(fadeOut, last->duration / 3.0));
    }
    // sound: a few milliseconds at every join (no clicks), a longer fade at both ends
    double firstStart = 1e18, lastEnd = 0;
    for (const Track& tr : tl.audio)
        for (const Clip& c : tr.clips)
            if (c.kind == ClipKind::Media) { firstStart = std::min(firstStart, c.start); lastEnd = std::max(lastEnd, c.end()); }
    for (Track& tr : tl.audio)
        for (Clip& c : tr.clips) {
            EffectInstance* v = c.effect("volume");
            if (!v || c.kind != ClipKind::Media) continue;
            const double tiny = std::min(0.02, c.duration / 4.0);
            v->set("fade_in", std::abs(c.start - firstStart) < 1e-6 && fadeIn > 0 ? std::min(fadeIn * 0.75, c.duration / 3.0) : tiny);
            v->set("fade_out", std::abs(c.end() - lastEnd) < 1e-6 && fadeOut > 0 ? std::min(fadeOut, c.duration / 3.0) : tiny);
        }
}

EditResult applyPlan(Timeline& tl, const EditPlan& plan, const QVector<MediaItem>& media, const PeaksProvider& peaks) {
    EditResult r;
    r.oldDuration = tl.duration();
    removeCaptions(tl);                 // start from the footage itself, so running a plan again never piles things up
    clearMotion(tl);

    // 1) dead air
    if (plan.style.silence) {
        const LevelMap map = buildLevelMap(tl, peaks);
        QVector<TimeRange> cuts;
        if (plan.cutByWaveform) cuts += findSilences(map, tl.fps, plan.silence);
        if (plan.cutSpeechGaps) cuts += speechGapCuts(tl, media, map, plan, &r.keptLoudGaps);
        cuts = mergeRanges(cuts, 0.0);
        for (const TimeRange& c : cuts) r.silenceSeconds += c.length();
        r.silences = int(cuts.size());
        if (!cuts.isEmpty()) ops::cutRanges(tl, cuts, true);
    }
    // 2) long takes are split at sentences, 3) zoom movements
    r.splits = splitLongShots(tl, plan, media);
    r.zoomed = applyMotion(tl, plan);
    const int mt = mainVideoTrack(tl);
    if (mt >= 0)
        for (const Clip& c : tl.video[mt].clips)
            if (c.kind == ClipKind::Media) ++r.shots;
    // 4) fades
    if (plan.style.fades) applyFades(tl, plan.fadeIn, plan.fadeOut);
    // 5) captions from the sentences that are left
    const QVector<TimelineSegment> segs = transcriptOnTimeline(tl, media);
    if (plan.style.captions && !segs.isEmpty()) {
        const QVector<TimelineSegment> parts = splitForCaptions(segs, plan.captionChars);
        addCaptions(tl, parts, plan.caption);
        r.captions = int(parts.size());
    }
    // sentences that may be wrong (poor sound, invented text ...): the user should check them
    QHash<QString, QVector<SegmentQuality>> quality;
    for (const MediaItem& m : media) {
        if (m.transcript.isEmpty()) continue;
        const QVector<quint8>* pk = peaks ? peaks(m.id) : nullptr;
        quality.insert(m.id, assessTranscript(m.transcript, pk));
    }
    for (const TimelineSegment& s : segs) {
        const auto it = quality.constFind(s.mediaId);
        if (it != quality.constEnd() && s.index >= 0 && s.index < it->size() && (*it)[s.index].quality != Quality::Good) ++r.doubtful;
    }
    r.newDuration = tl.duration();
    return r;
}

}  // namespace hr::ai
