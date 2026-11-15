#pragma once
#include "Cleanup.h"
#include "Silence.h"
#include "Transcript.h"
#include "core/Model.h"

#include <QString>
#include <QStringList>
#include <QVector>

namespace hr::ai {

// ---- the "smart edit": look at the footage, decide what suits it, apply it --------------------------------------
//
//   analyzeProject()  measures the footage (length, sound, speech, speaking speed, shape of the picture)
//   planEdit()        turns the measurements and the wishes of the user (EditStyle) into concrete settings
//   applyPlan()       does it on a Timeline: cuts the dead air, splits long shots, adds zoom movements, fades, captions
//
// All three are plain functions (no UI), so the assistant can run them again on the untouched sequence with other
// wishes ("less zoom", "no captions") and every result is a clean one.

enum class Pace { Calm, Balanced, Fast };

// What the user asks for.  NaN means "decide for me".
struct EditStyle {
    bool silence = true;          // cut the dead air
    bool captions = true;
    bool motion = true;           // zoom / punch-in movements between shots
    bool fades = true;            // fade in at the start, fade out at the end
    Pace pace = Pace::Balanced;
    double thresholdDb = qQNaN();
    double minSilence = qQNaN();  // seconds
    double padding = qQNaN();     // seconds kept around speech
    double motionStrength = 1.0;  // 0 none ... 2 twice as strong
    QString captionColor;         // empty = white
    double captionSize = 0;       // 0 = decide
    double captionY = qQNaN();    // % of the height below the centre, NaN = decide
};

// What the footage is like.
struct VideoProfile {
    double duration = 0;
    int shots = 0;                // media clips on the main video track
    bool hasVideo = false;
    bool vertical = false;
    AudioStats audio;
    bool hasTranscript = false;
    int sentences = 0;
    double speechRatio = -1;      // share of the time with recognised speech, -1 = unknown
    double wordsPerSecond = 0;    // while someone is talking
};

// The decisions (every "decide for me" resolved) plus the reasons, as keys the interface turns into sentences.
struct EditPlan {
    EditStyle style;
    SilenceOptions silence;
    bool cutByWaveform = true;    // false when the background noise is too strong for the waveform to tell pauses
    bool cutSpeechGaps = false;   // also cut the gaps between recognised sentences that are only noise
    double speechDb = -20, noiseDb = -60;
    double maxShot = 7;           // longest shot before it is split at a sentence (seconds)
    bool punch = true;            // every other shot is zoomed in
    double punchPercent = 12;
    double pushPercent = 4;       // slow zoom during a shot
    double fadeIn = 0.4, fadeOut = 0.6;
    int captionChars = 48;
    CaptionStyle caption;
    QStringList reasons;          // "talking", "fast-talker", "slow-talker", "noisy", "quiet", "no-speech", "vertical", "long-takes", "calm", "fast"
};

struct EditResult {
    int silences = 0;
    double silenceSeconds = 0;
    int keptLoudGaps = 0;         // gaps with sound but no recognised words: kept (maybe missed speech)
    int splits = 0;               // long shots split at sentences
    int shots = 0;
    int zoomed = 0;
    int captions = 0;
    int doubtful = 0;             // sentences flagged as possibly wrong
    double oldDuration = 0, newDuration = 0;
};

VideoProfile analyzeProject(const Timeline& tl, const QVector<MediaItem>& media, const PeaksProvider& peaks);
EditPlan planEdit(const VideoProfile& profile, const EditStyle& style);
EditResult applyPlan(Timeline& tl, const EditPlan& plan, const QVector<MediaItem>& media, const PeaksProvider& peaks);

// Pieces the assistant also uses on their own.
int mainVideoTrack(const Timeline& tl);                      // the video track with the most footage, -1 when none
int applyMotion(Timeline& tl, const EditPlan& plan);         // zoom movements on the main track; returns the clips changed
int clearMotion(Timeline& tl);                               // takes the assistant's movements away again
void applyFades(Timeline& tl, double fadeIn, double fadeOut);
void clearFades(Timeline& tl);

}  // namespace hr::ai
