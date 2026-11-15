#pragma once
#include <QString>
#include <QStringList>
#include <QtGlobal>

namespace hr::ai {

// The chat of the assistant understands requests written in Arabic (formal or the spoken dialects) or English.
// It runs on this computer with plain rules: no account, no key, no internet.  It is not a general language model:
// it knows the things the assistant can do, in many ways of saying them, and says so when it does not understand.
enum class Intent {
    Unknown,
    Help, HowToEditText, Greeting, Thanks, Yes, No,
    FullEdit,                 // a complete edit; preset / without-lists say what kind
    RemoveSilence,
    AddCaptions, RemoveCaptions, CaptionStyle,
    MotionOn, MotionOff, MotionMore, MotionLess,
    FadesOn, FadesOff,
    ChangeSpeed,
    Undo, Redo,
    Transcribe, BetterAccuracy, CleanAudio, SetLanguage,
    ReplaceWord, EditSentence, DeleteSentences, KeepSentences,
    ShowDoubtful, Summary, Scenes, ExportSrt,
};

struct Command {
    Intent intent = Intent::Unknown;
    QString from, to;                  // ReplaceWord: from -> to.  EditSentence: `to` = the new text.  Delete / Keep: `from` = the word to look for
    int sentence = 0;                  // 1-based sentence number, -1 = the last one, 0 = not given
    double seconds = qQNaN();          // a length of time ("longer than half a second")
    double number = qQNaN();           // a speed factor, ...
    QString color;                     // "#rrggbb"
    QString position;                  // "top" | "center" | "bottom"
    bool bigger = false, smaller = false;
    QString preset;                    // FullEdit: "fast" | "calm" | "" (balanced)
    QStringList without;               // FullEdit: parts the user does not want: "captions", "motion", "silence", "fades"
    bool cutScenes = false;            // Scenes: cut there (true) or only put markers (false)
    QString language;                  // SetLanguage: "ar", "en", ...
    QString level;                     // CleanAudio: "strong"
    QString normalized;                // what the rules saw (for tests and debugging)
};

Command parseChat(const QString& text);

}  // namespace hr::ai
