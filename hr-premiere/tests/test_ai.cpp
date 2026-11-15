// Tests for the AI assistant's logic (no window needed): Arabic text matching, transcript edits, motion,
// transcript quality, audio clean-up, the smart edit planner and the chat command parser.
#include "ai/ChatParser.h"
#include "ai/Cleanup.h"
#include "ai/Silence.h"
#include "ai/SmartEdit.h"
#include "ai/Transcript.h"
#include "ai/Whisper.h"
#include "core/EditOps.h"
#include "core/I18n.h"
#include "core/Project.h"
#include "core/TextUtil.h"
#include "engine/DecoderPool.h"
#include "engine/Exporter.h"
#include "engine/Renderer.h"

#include <QDirIterator>
#include <QProcess>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QtTest>
#include <cmath>

using namespace hr;

static bool runFfmpeg(const QStringList& args) {
    QProcess p;
    p.start(Exporter::findFfmpeg().isEmpty() ? "ffmpeg" : Exporter::findFfmpeg(),
            QStringList{"-hide_banner", "-loglevel", "error", "-y"} + args);
    if (!p.waitForFinished(60000)) return false;
    if (p.exitCode() != 0) qWarning() << p.readAllStandardError();
    return p.exitCode() == 0;
}

// Horizontal extent / centre of mass of the non-black pixels of a frame.
struct Blob { int left = -1, right = -1, top = -1, bottom = -1; double cx = 0; };
static Blob blobOf(const QImage& img) {
    Blob b;
    double sumX = 0;
    long n = 0;
    for (int y = 0; y < img.height(); ++y)
        for (int x = 0; x < img.width(); ++x) {
            const QColor c(img.pixel(x, y));
            if (c.red() + c.green() + c.blue() < 90) continue;
            if (b.left < 0 || x < b.left) b.left = x;
            if (x > b.right) b.right = x;
            if (b.top < 0 || y < b.top) b.top = y;
            if (y > b.bottom) b.bottom = y;
            sumX += x;
            ++n;
        }
    b.cx = n ? sumX / double(n) : -1;
    return b;
}


// A talking-head clip of `dur` seconds that exists only as data (no file): enough for the planner and the editor.
static MediaItem talkMedia(double dur) {
    MediaItem m;
    m.id = "talk";
    m.name = "talk.mp4";
    m.path = "/nonexistent/talk.mp4";
    m.kind = MediaKind::Video;
    m.duration = dur;
    m.width = 1920;
    m.height = 1080;
    m.fps = 30;
    m.hasVideo = m.hasAudio = true;
    return m;
}
static const char* kSentence = "this is a sentence with about fifteen words so that the speed estimate stays stable";

class AiTests : public QObject {
    Q_OBJECT
    QTemporaryDir dir;

private slots:
    void initTestCase() { QVERIFY(dir.isValid()); }

    // ------------------------------------------------------------------ the chat understands requests
    void theChatUnderstandsArabicAndEnglishRequests() {
        using I = ai::Intent;
        struct Row { const char* text; I intent; };
        const Row rows[] = {
            // the whole edit (the first two are the user's own words)
            {"خلي يعمل مونتاج كامل حركات ويشيل القطات الي انا فيها ساكت", I::FullEdit},
            {"اعمل مونتاج كامل", I::FullEdit}, {"مونتاج تلقائي للفيديو", I::FullEdit}, {"اعملي مونتاج سريع للتيك توك", I::FullEdit},
            {"مونتاج هادي سينمائي", I::FullEdit}, {"حرر الفيديو", I::FullEdit}, {"حسّن الفيديو", I::FullEdit}, {"رتب الفيديو", I::FullEdit},
            {"edit my video automatically", I::FullEdit}, {"full edit please", I::FullEdit}, {"auto edit", I::FullEdit},
            {"مونتاج كامل بدون ترجمة", I::FullEdit}, {"مونتاج كامل بدون زوم وبدون فيد", I::FullEdit},
            // dead air
            {"شيل الصمت", I::RemoveSilence}, {"احذف الأماكن الساكتة", I::RemoveSilence}, {"شيل القطات الساكتة", I::RemoveSilence},
            {"remove the silences", I::RemoveSilence}, {"قص السكوت", I::RemoveSilence}, {"cut the dead air silence", I::RemoveSilence},
            {"الصمت اللي اطول من ثانيتين شيله", I::RemoveSilence}, {"remove pauses longer than 1.5 seconds silence", I::RemoveSilence},
            // captions
            {"ضيف ترجمة", I::AddCaptions}, {"اكتب الكلام على الشاشة", I::AddCaptions}, {"add subtitles", I::AddCaptions}, {"ضيفلي كابشن", I::AddCaptions},
            {"شيل الترجمة", I::RemoveCaptions}, {"بدون ترجمة", I::RemoveCaptions}, {"remove the captions", I::RemoveCaptions}, {"احذف السبتايتل", I::RemoveCaptions},
            {"خلي الترجمة صفراء", I::CaptionStyle}, {"الترجمة لونها أصفر", I::CaptionStyle}, {"كبّر الترجمة", I::CaptionStyle},
            {"صغّر الترجمة وخليها فوق", I::CaptionStyle}, {"make the captions bigger", I::CaptionStyle}, {"الترجمة بالنص", I::CaptionStyle},
            {"ضيف ترجمة بالأحمر", I::AddCaptions},
            // movement and fades
            {"ضيف زوم", I::MotionOn}, {"اعمل حركات", I::MotionOn}, {"add some zoom", I::MotionOn}, {"بدون زوم", I::MotionOff}, {"شيل الحركات", I::MotionOff},
            {"no zoom", I::MotionOff}, {"زوم اقل", I::MotionLess}, {"خفف الزوم", I::MotionLess}, {"less movement", I::MotionLess},
            {"زوم اكثر", I::MotionMore}, {"make the zoom stronger", I::MotionMore}, {"ضيف فيد", I::FadesOn}, {"بدون فيد", I::FadesOff},
            // speed
            {"سرّع الفيديو 2x", I::ChangeSpeed}, {"بطّئ الفيديو", I::ChangeSpeed}, {"سرع الفيديو ضعفين", I::ChangeSpeed}, {"slow down by 3", I::ChangeSpeed},
            // undo / redo
            {"تراجع", I::Undo}, {"ارجع زي ما كان", I::Undo}, {"undo", I::Undo}, {"الغي اخر شي", I::Undo}, {"اعادة", I::Redo}, {"redo", I::Redo},
            // text
            {"غيّر كلمة \"اهلا\" إلى \"مرحبا\"", I::ReplaceWord}, {"بدّل مرحبا ب اهلا", I::ReplaceWord}, {"replace hello with hi", I::ReplaceWord},
            {"change the word color to colour", I::ReplaceWord},
            {"عدل الجملة 3 الى اهلا وسهلا", I::EditSentence}, {"الجملة الثانية خليها «مرحبا بكم»", I::EditSentence}, {"change sentence 2 to hello there", I::EditSentence},
            {"احذف الجملة 2", I::DeleteSentences}, {"شيل كل جملة فيها كلمة اشتركوا", I::DeleteSentences}, {"احذف اخر جملة", I::DeleteSentences},
            {"delete the last sentence", I::DeleteSentences}, {"خلي بس الجمل اللي فيها مونتاج", I::KeepSentences},
            {"الكلام فيه اخطاء", I::ShowDoubtful}, {"النص مش صح", I::ShowDoubtful}, {"some of the text is wrong", I::ShowDoubtful}, {"وين الاخطاء بالكلام", I::ShowDoubtful},
            {"عدل النص", I::HowToEditText}, {"اقدر اعدل النص", I::HowToEditText},
            // sound and recognition
            {"الصوت مش واضح", I::CleanAudio}, {"في ضجيج بالفيديو", I::CleanAudio}, {"نظف الصوت", I::CleanAudio}, {"the audio is noisy", I::CleanAudio},
            {"اعد التفريغ بدقة أعلى", I::BetterAccuracy}, {"اكثر دقة", I::BetterAccuracy}, {"transcribe again more accurately", I::BetterAccuracy},
            {"فرغ الكلام", I::Transcribe}, {"حول الكلام الي نص", I::Transcribe}, {"transcribe the video", I::Transcribe},
            // scenes, info, files
            {"قص عند تغير المشاهد", I::Scenes}, {"حط علامات عند المشاهد", I::Scenes}, {"detect scenes", I::Scenes}, {"cut at scene changes", I::Scenes},
            {"لخص الفيديو", I::Summary}, {"شو في بالفيديو", I::Summary}, {"how long is it", I::Summary}, {"صدّر الترجمة", I::ExportSrt}, {"export the subtitles", I::ExportSrt},
            {"الكلام بالانجليزي", I::SetLanguage}, {"the speech is in english", I::SetLanguage},
            // small talk
            {"ساعدني", I::Help}, {"شو بتقدر تعمل", I::Help}, {"help", I::Help}, {"what can you do", I::Help},
            {"مرحبا", I::Greeting}, {"هلا", I::Greeting}, {"hello", I::Greeting}, {"شكراً", I::Thanks}, {"thanks", I::Thanks},
            {"نعم", I::Yes}, {"اه", I::Yes}, {"yes", I::Yes}, {"ok", I::Yes}, {"لا", I::No}, {"no", I::No},
            {"كيف حال الطقس اليوم", I::Unknown}, {"", I::Unknown}, {"   ", I::Unknown},
        };
        QStringList wrong;
        for (const Row& r : rows) {
            const ai::Command c = ai::parseChat(QString::fromUtf8(r.text));
            if (c.intent != r.intent) wrong << QString("\"%1\"  expected %2, got %3  (%4)").arg(QString::fromUtf8(r.text)).arg(int(r.intent)).arg(int(c.intent)).arg(c.normalized);
        }
        QVERIFY2(wrong.isEmpty(), qPrintable(QString("%1 misunderstood:\n  ").arg(wrong.size()) + wrong.join("\n  ")));
    }

    void theChatPicksUpTheDetails() {
        using I = ai::Intent;
        auto cmd = [](const char* t) { return ai::parseChat(QString::fromUtf8(t)); };
        // presets and the parts the user does not want
        ai::Command c = cmd("اعملي مونتاج سريع للتيك توك");
        QCOMPARE(c.preset, QString("fast"));
        QCOMPARE(cmd("مونتاج هادي سينمائي").preset, QString("calm"));
        QCOMPARE(cmd("اعمل مونتاج كامل").preset, QString());
        c = cmd("مونتاج كامل بدون زوم وبدون فيد");
        QVERIFY(c.without.contains("motion") && c.without.contains("fades") && !c.without.contains("captions"));
        QVERIFY(cmd("مونتاج كامل بدون ترجمة").without.contains("captions"));
        QVERIFY(cmd("full edit without captions").without.contains("captions"));
        // lengths of time
        QVERIFY(std::abs(cmd("شيل الصمت اللي اطول من نص ثانية").seconds - 0.5) < 1e-9);
        QVERIFY(std::abs(cmd("شيل الصمت اللي اطول من ثانيتين").seconds - 2.0) < 1e-9);
        QVERIFY(std::abs(cmd("شيل الصمت اللي اطول من ثانية ونص").seconds - 1.5) < 1e-9);
        QVERIFY(std::abs(cmd("شيل الصمت اللي اطول من 3 ثواني").seconds - 3.0) < 1e-9);
        QVERIFY(std::abs(cmd("remove silence longer than 0.8s").seconds - 0.8) < 1e-9);
        QVERIFY(std::abs(cmd("remove silences longer than 1.5 seconds").seconds - 1.5) < 1e-9);
        QVERIFY(std::isnan(cmd("شيل الصمت").seconds));
        // caption look
        c = cmd("خلي الترجمة صفراء");
        QCOMPARE(c.color, QString("#ffd60a"));
        QCOMPARE(cmd("make the captions red").color, QString("#ff3b30"));
        QVERIFY(cmd("كبّر الترجمة").bigger && !cmd("كبّر الترجمة").smaller);
        c = cmd("صغّر الترجمة وخليها فوق");
        QVERIFY(c.smaller);
        QCOMPARE(c.position, QString("top"));
        QCOMPARE(cmd("الترجمة تحت").position, QString("bottom"));
        QCOMPARE(cmd("الترجمة بالنص").position, QString("center"));
        c = cmd("ضيف ترجمة بالأحمر وكبيرة");
        QCOMPARE(c.intent, I::AddCaptions);
        QCOMPARE(c.color, QString("#ff3b30"));
        QVERIFY(c.bigger);
        // speed
        QVERIFY(std::abs(cmd("سرّع الفيديو 2x").number - 2.0) < 1e-9);
        QVERIFY(std::abs(cmd("بطّئ الفيديو").number - 0.5) < 1e-9);
        QVERIFY(std::abs(cmd("سرع الفيديو 3 مرات").number - 3.0) < 1e-9);
        // words and sentences
        c = cmd("غيّر كلمة \"اهلا\" إلى \"مرحبا\"");
        QCOMPARE(c.from, QString::fromUtf8("اهلا"));
        QCOMPARE(c.to, QString::fromUtf8("مرحبا"));
        c = cmd("بدّل مرحبا ب اهلا وسهلا");
        QCOMPARE(c.from, QString::fromUtf8("مرحبا"));
        QCOMPARE(c.to, QString::fromUtf8("اهلا وسهلا"));
        c = cmd("replace the word hello with hi there");
        QCOMPARE(c.from, QString("hello"));
        QCOMPARE(c.to, QString("hi there"));
        c = cmd("عدل الجملة 3 الى اهلا وسهلا بكم");
        QCOMPARE(c.sentence, 3);
        QCOMPARE(c.to, QString::fromUtf8("اهلا وسهلا بكم"));
        QCOMPARE(cmd("الجملة الثانية خليها «مرحبا بكم»").sentence, 2);
        QCOMPARE(cmd("الجملة الثانية خليها «مرحبا بكم»").to, QString::fromUtf8("مرحبا بكم"));
        QCOMPARE(cmd("احذف الجملة 2").sentence, 2);
        QCOMPARE(cmd("احذف اخر جملة").sentence, -1);
        QCOMPARE(cmd("شيل كل جملة فيها كلمة اشتركوا").from, QString::fromUtf8("اشتركوا"));
        QCOMPARE(cmd("delete every sentence containing \"subscribe\"").from, QString("subscribe"));
        QCOMPARE(cmd("delete every sentence with the word third").from, QString("third"));
        QCOMPARE(cmd("delete every sentence that has the word third").from, QString("third"));
        QCOMPARE(cmd("remove the sentences having hello world").from, QString("hello world"));
        QCOMPARE(cmd("keep only the sentences with the word offer").intent, I::KeepSentences);
        QCOMPARE(cmd("keep only the sentences with the word offer").from, QString("offer"));
        QCOMPARE(cmd("احذف كل جملة فيها كلمة مرحبا").from, QString::fromUtf8("مرحبا"));
        QCOMPARE(cmd("احذف كل جملة فيها مرحبا").from, QString::fromUtf8("مرحبا"));
        QCOMPARE(cmd("قص عند تغير المشاهد").cutScenes, true);
        QCOMPARE(cmd("حط علامات عند المشاهد").cutScenes, false);
        QCOMPARE(cmd("الكلام بالانجليزي").language, QString("en"));
        QCOMPARE(cmd("الصوت مش واضح").level, QString("strong"));
        QCOMPARE(cmd("نظف الصوت").level, QString("light"));
    }

    // ------------------------------------------------------------------ the two languages
    void everyTextOfTheProgramHasAnArabicTranslation() {
        // collect every TR("...") of the sources (adjacent string literals are one text)
        const QRegularExpression call("\\bTR\\(\\s*((?:\"(?:[^\"\\\\]|\\\\.)*\"\\s*)+)[,)]");
        const QRegularExpression piece("\"((?:[^\"\\\\]|\\\\.)*)\"");
        auto unescape = [](QString s) {
            QString out;
            for (int i = 0; i < s.size(); ++i) {
                if (s[i] == '\\' && i + 1 < s.size()) {
                    const QChar n = s[++i];
                    out += n == 'n' ? QChar('\n') : n == 't' ? QChar('\t') : n;
                } else out += s[i];
            }
            return out;
        };
        QStringList missing;
        int checked = 0;
        for (const QString sub : {"src/ui", "src/ai", "src/engine", "src/media", "src/core"}) {
            QDirIterator it(QString(QT_TESTCASE_SOURCEDIR) + "/" + sub, {"*.cpp"}, QDir::Files);
            while (it.hasNext()) {
                QFile f(it.next());
                if (QFileInfo(f).fileName().startsWith("I18n")) continue;      // the table itself talks about TR() in comments
                QVERIFY(f.open(QIODevice::ReadOnly));
                const QString src = QString::fromUtf8(f.readAll());
                auto m = call.globalMatch(src);
                while (m.hasNext()) {
                    QString text;
                    auto pm = piece.globalMatch(m.next().captured(1));
                    while (pm.hasNext()) text += pm.next().captured(1);
                    text = unescape(text);
                    ++checked;
                    if (!i18n::hasArabic(text)) missing << QFileInfo(f).fileName() + ": " + text;
                }
            }
        }
        QVERIFY2(checked > 300, qPrintable(QString::number(checked)));
        QVERIFY2(missing.isEmpty(), qPrintable("no Arabic text for:\n  " + missing.join("\n  ")));
        // texts that are stored as const char* and translated when they are shown
        for (const char* t : {"Selection Tool (V)", "Track Select Forward Tool (A)", "Ripple Edit Tool (B)", "Razor Tool (C)", "Hand Tool (H)",
                              "Zoom Tool (Z)", "Type Tool (T) – click a video track to add a title", "Editing", "Assembly", "Color", "Effects", "Audio",
                              "AI", "Selection", "Track Select Forward", "Ripple Edit", "Razor", "Hand", "Zoom", "Type", "Custom"})
            QVERIFY2(i18n::hasArabic(QString::fromUtf8(t)), t);
    }

    void namesTakenFromListsAreTranslatedToo() {
        // texts that reach TR() through a variable (the scan above cannot see them)
        QStringList missing;
        auto need = [&](const QString& what, const QString& text) {
            if (!text.isEmpty() && !i18n::hasArabic(text)) missing << what + ": " + text;
        };
        for (const EffectDef& d : EffectRegistry::all()) {
            need("effect", d.name);
            need("category", d.category);
            for (const ParamDef& p : d.params) {
                need("parameter of " + d.name, p.label);
                for (const QString& c : p.choices) need("choice of " + p.label, c);
            }
        }
        for (bool video : {true, false})
            for (const QString& c : EffectRegistry::categories(video)) need("category", c);
        for (const ai::ModelInfo& m : ai::AiModels::models()) need("speech model", m.title);
        for (const char* l : {"Auto-detect", "Arabic", "English", "French", "Spanish", "German", "Turkish", "Persian", "Urdu", "Hindi", "Russian",
                              "Italian", "Portuguese", "Indonesian", "Japanese", "Korean", "Chinese"})
            need("language", QString::fromUtf8(l));
        for (const char* l : {"Custom", "HD 1080p  (1920 × 1080)", "HD 720p  (1280 × 720)", "4K UHD  (3840 × 2160)",
                              "Vertical 1080 × 1920 (Reels / TikTok)", "Square 1080 × 1080"})
            need("preset", QString::fromUtf8(l));
        QVERIFY2(missing.isEmpty(), qPrintable("no Arabic text for:\n  " + missing.join("\n  ")));
    }

    void arabicEntriesKeepTheirPlaceholders() {
        static const QRegularExpression mark("%\\d");
        for (auto it = i18n::arabicCatalog().cbegin(); it != i18n::arabicCatalog().cend(); ++it) {
            QStringList a, b;
            auto m1 = mark.globalMatch(it.key());
            while (m1.hasNext()) a << m1.next().captured();
            auto m2 = mark.globalMatch(it.value());
            while (m2.hasNext()) b << m2.next().captured();
            a.sort();
            b.sort();
            QVERIFY2(a == b, qPrintable(it.key()));
        }
    }

    void theLanguageSwitchChangesWhatTRReturns() {
        const auto before = i18n::current();
        i18n::setCurrent(i18n::Lang::English);
        QCOMPARE(TR("Save"), QString("Save"));
        i18n::setCurrent(i18n::Lang::Arabic);
        QCOMPARE(TR("Save"), QString::fromUtf8("حفظ"));
        QCOMPARE(TR("This text has no translation"), QString("This text has no translation"));   // falls back to English
        QVERIFY(i18n::isRtl());
        QCOMPARE(i18n::fromCode("ar-SA"), i18n::Lang::Arabic);
        QCOMPARE(i18n::fromCode("EN"), i18n::Lang::English);
        QCOMPARE(i18n::fromCode("fr", i18n::Lang::Arabic), i18n::Lang::Arabic);
        i18n::setCurrent(before);
    }

    // ------------------------------------------------------------------ Arabic text
    void arabicNormalisationFoldsTheUsualVariants() {
        QCOMPARE(normalizeArabic(QString::fromUtf8("أَهْلاً وسهلاً بالتَّرْجَمَة؟")).simplified(), QString::fromUtf8("اهلا وسهلا بالترجمه"));
        QCOMPARE(normalizeArabic(QString::fromUtf8("٢٠٢٤ و ۱۲")), QString("2024 و 12"));
        QCOMPARE(normalizeArabic("Hello, World!").simplified(), QString("hello world"));
        QCOMPARE(normalizeArabic(QString::fromUtf8("على")), QString::fromUtf8("علي"));
    }

    void tolerantSearchIgnoresDiacriticsAndHamza() {
        const QString t = QString::fromUtf8("أهلاً وسهلاً بكم في القناة");
        QVERIFY(containsTolerant(t, QString::fromUtf8("اهلا")));
        QVERIFY(containsTolerant(t, QString::fromUtf8("القناه")));            // ة / ه
        QVERIFY(containsTolerant(t, QString::fromUtf8("وسهلا بكم")));
        QVERIFY(!containsTolerant(t, QString::fromUtf8("الترجمه")));
        QVERIFY(!containsTolerant(t, ""));
        QVERIFY(containsTolerant("Hello World", "hello  world"));
    }

    // ------------------------------------------------------------------ transcript edits (undoable)
    void sentenceEditsAndWordReplacementCanBeUndone() {
        Project p;
        QString err;
        const QString wav = dir.filePath("t.wav");
        QVERIFY(runFfmpeg({"-f", "lavfi", "-i", "sine=frequency=300:duration=3", wav}));
        const QString id = p.importFile(wav, &err);
        QVERIFY2(!id.isEmpty(), qPrintable(err));
        p.setTranscript(id,
                        {{0.0, 1.0, QString::fromUtf8("أهلاً وسهلاً بكم")},
                         {1.0, 2.0, QString::fromUtf8("اليوم رح نحكي عن المونتاج")},
                         {2.0, 3.0, QString::fromUtf8("شكراً لكم")}},
                        "ar", "small");
        const int base = p.undoStack()->index();

        // one sentence corrected by hand: marked as checked, one undo step
        QVERIFY(p.editSentence(id, 1, QString::fromUtf8("  اليوم رح نحكي   عن الفيديو ")));
        QCOMPARE(p.mediaById(id)->transcript[1].text, QString::fromUtf8("اليوم رح نحكي عن الفيديو"));
        QVERIFY(p.mediaById(id)->transcript[1].edited);
        QVERIFY(!p.mediaById(id)->transcript[0].edited);
        QVERIFY(!p.editSentence(id, 1, QString::fromUtf8("اليوم رح نحكي عن الفيديو")));   // nothing changed
        QVERIFY(!p.editSentence(id, 9, "x"));
        QCOMPARE(p.undoStack()->index(), base + 1);
        p.undoStack()->undo();
        QCOMPARE(p.mediaById(id)->transcript[1].text, QString::fromUtf8("اليوم رح نحكي عن المونتاج"));
        QVERIFY(!p.mediaById(id)->transcript[1].edited);

        // replacing a word ignores hamza / diacritics, counts every place and is one undo step
        QCOMPARE(p.replaceInTranscripts(QString::fromUtf8("اهلا"), QString::fromUtf8("مرحبا")), 1);
        QCOMPARE(p.mediaById(id)->transcript[0].text, QString::fromUtf8("مرحبا وسهلاً بكم"));
        QCOMPARE(p.replaceInTranscripts(QString::fromUtf8("لكم"), QString::fromUtf8("لك")), 1);
        QCOMPARE(p.replaceInTranscripts("nothing like this", "x"), 0);
        p.undoStack()->undo();
        p.undoStack()->undo();
        QCOMPARE(p.mediaById(id)->transcript[0].text, QString::fromUtf8("أهلاً وسهلاً بكم"));
        QCOMPARE(p.mediaById(id)->transcript[2].text, QString::fromUtf8("شكراً لكم"));

        // the "checked by me" flag survives saving
        QVERIFY(p.editSentence(id, 2, QString::fromUtf8("شكراً جزيلاً")));
        const QString file = dir.filePath("edited.hrproj");
        QVERIFY(p.save(file));
        Project q;
        QVERIFY(q.load(file));
        QVERIFY(q.media().first().transcript[2].edited);
        QVERIFY(!q.media().first().transcript[0].edited);
        QCOMPARE(q.media().first().transcript[2].text, QString::fromUtf8("شكراً جزيلاً"));
    }


    // ------------------------------------------------------------------ how reliable is the text?
    void poorSoundAndInventedSentencesAreFlagged() {
        // 20 s of sound, one byte per 10 ms: background noise at 6, loud speech 3-5 s and 13-15 s, weak speech elsewhere
        QVector<quint8> peaks(2000, 6);
        auto fill = [&](double a, double b, quint8 v) { for (int i = int(a * 100); i < int(b * 100); ++i) peaks[i] = v; };
        fill(0, 3, 150);       // A: loud
        fill(4, 6, 14);        // B: barely above the noise
        fill(7, 9, 25);        // C: a bit weak
        fill(10, 12, 14);      // D: "thanks for watching" on noise
        fill(13, 15, 150);     // E: "thanks for watching" for real, loud
        fill(16, 17, 150);     // H: far too many letters
        const QVector<TranscriptSegment> segs = {
            {0, 3, QString::fromUtf8("أهلاً بكم في هذا الفيديو")},
            {4, 6, QString::fromUtf8("نحكي اليوم عن شي مهم")},
            {7, 9, QString::fromUtf8("كمان شغله ثانيه")},
            {10, 12, "Thanks for watching!"},
            {13, 15, "Thank you for watching, see you in the next one."},
            {15.2, 15.8, "no no no no no no no"},
            {16, 17, QString("a").repeated(70)},
        };
        const QVector<ai::SegmentQuality> q = ai::assessTranscript(segs, &peaks);
        QCOMPARE(q.size(), segs.size());
        QCOMPARE(q[0].quality, ai::Quality::Good);
        QVERIFY2(q[0].snrDb > 20, qPrintable(QString::number(q[0].snrDb)));
        QCOMPARE(q[1].quality, ai::Quality::Poor);
        QVERIFY(q[1].reasons.contains("noisy"));
        QCOMPARE(q[2].quality, ai::Quality::Doubtful);
        QCOMPARE(q[3].quality, ai::Quality::Poor);
        QVERIFY(q[3].reasons.contains("hallucination"));
        QCOMPARE(q[4].quality, ai::Quality::Good);                    // a real sign-off at a normal level is kept
        QCOMPARE(q[5].quality, ai::Quality::Poor);
        QVERIFY(q[5].reasons.contains("repeated"));
        QVERIFY(q[6].reasons.contains("too-fast"));
        // sentences the user corrected are trusted, whatever the sound was like
        QVector<TranscriptSegment> fixed = segs;
        fixed[1].edited = true;
        QCOMPARE(ai::assessTranscript(fixed, &peaks)[1].quality, ai::Quality::Good);
        // without a waveform only the text is judged
        const QVector<ai::SegmentQuality> textOnly = ai::assessTranscript(segs, nullptr);
        QCOMPARE(textOnly[0].quality, ai::Quality::Good);
        QCOMPARE(textOnly[5].quality, ai::Quality::Poor);
    }

    void inventedSentencesAndLoopsAreDroppedBeforeStoring() {
        QVector<quint8> peaks(1200, 5);
        for (int i = 0; i < 300; ++i) peaks[i] = 150;               // speech 0-3 s, then noise
        const QVector<TranscriptSegment> segs = {
            {0, 3, QString::fromUtf8("مرحبا بكم")},
            {5, 7, QString::fromUtf8("شكرا للمشاهدة")},               // invented on noise
            {7, 8, "hello hello"},
            {8, 9, "hello hello"},
            {9, 10, "hello hello"},                                   // third in a row = a loop
            {10, 11, "   "},
        };
        int removed = 0;
        const QVector<TranscriptSegment> out = ai::sanitizeTranscript(segs, &peaks, &removed);
        QCOMPARE(removed, 3);
        QCOMPARE(out.size(), 3);
        QCOMPARE(out[0].text, QString::fromUtf8("مرحبا بكم"));
        QCOMPARE(out[2].text, QString("hello hello"));
    }

    // ------------------------------------------------------------------ the smart edit
    void thePlannerAdaptsToTheFootage() {
        auto profile = [](double noise, double speech, double wps, bool transcript) {
            ai::VideoProfile p;
            p.duration = 60;
            p.hasVideo = true;
            p.audio.valid = true;
            p.audio.noiseFloorDb = noise;
            p.audio.speechDb = speech;
            p.hasTranscript = transcript;
            p.speechRatio = transcript ? 0.8 : -1;
            p.wordsPerSecond = wps;
            return p;
        };
        // a clean, fast talker: short pauses are cut, waveform + sentences are used, shots are punched in
        ai::EditPlan fast = ai::planEdit(profile(-55, -8, 3.6, true), ai::EditStyle());
        QVERIFY(std::abs(fast.silence.minSilence - 0.35) < 1e-9);
        QVERIFY2(fast.silence.thresholdDb > -52 && fast.silence.thresholdDb < -30, qPrintable(QString::number(fast.silence.thresholdDb)));
        QVERIFY(fast.cutByWaveform && fast.cutSpeechGaps && fast.punch);
        QVERIFY(fast.reasons.contains("talking") && fast.reasons.contains("fast-talker"));
        // a slow talker gets longer pauses before anything is cut
        QVERIFY(std::abs(ai::planEdit(profile(-55, -8, 1.5, true), ai::EditStyle()).silence.minSilence - 0.6) < 1e-9);
        // strong background noise: the waveform cannot find pauses, the sentences do
        ai::EditPlan noisy = ai::planEdit(profile(-30, -12, 2.8, true), ai::EditStyle());
        QVERIFY(!noisy.cutByWaveform && noisy.cutSpeechGaps);
        QVERIFY(noisy.reasons.contains("noisy"));
        // too noisy even for that, or nothing recognised: nothing is guessed
        QVERIFY(!ai::planEdit(profile(-25, -15, 2.8, true), ai::EditStyle()).cutSpeechGaps);
        QVERIFY(!ai::planEdit(profile(-30, -12, 0, false), ai::EditStyle()).cutByWaveform);
        // an explicit threshold from the user is respected even then
        ai::EditStyle forced;
        forced.thresholdDb = -33;
        QVERIFY(ai::planEdit(profile(-30, -12, 0, false), forced).cutByWaveform);
        // the user's wishes win over the automatic choice
        ai::EditStyle own;
        own.minSilence = 1.2;
        own.motionStrength = 0.5;
        own.pace = ai::Pace::Fast;
        const ai::EditPlan custom = ai::planEdit(profile(-55, -8, 3.6, true), own);
        QVERIFY(std::abs(custom.silence.minSilence - 1.2) < 1e-9);
        QVERIFY(std::abs(custom.punchPercent - 7.0) < 1e-9 && std::abs(custom.maxShot - 4.5) < 1e-9);
        ai::EditStyle none;
        none.motion = false;
        const ai::EditPlan still = ai::planEdit(profile(-55, -8, 3.6, true), none);
        QCOMPARE(still.punchPercent + still.pushPercent, 0.0);
        const ai::EditPlan calm = ai::planEdit(profile(-55, -8, 3.6, true), [] { ai::EditStyle e; e.pace = ai::Pace::Calm; return e; }());
        QVERIFY(!calm.punch && std::abs(calm.maxShot - 11) < 1e-9);
        ai::VideoProfile vertical = profile(-55, -8, 3, true);
        vertical.vertical = true;
        const ai::EditPlan tall = ai::planEdit(vertical, ai::EditStyle());
        QVERIFY(tall.captionChars < 30 && tall.caption.size < 40);
    }

    void theSmartEditCutsSplitsZoomsFadesAndCaptions() {
        const MediaItem m = talkMedia(30);
        Timeline tl = Timeline::makeDefault();
        ops::placeClips(tl, ops::makeClips(m, 0, 30, 0), 0, 0, false);
        // speech 0-6, silence 6-8, speech 8-16, silence 16-17, speech 17-30
        QVector<quint8> peaks(3000, 5);
        auto speech = [&](double a, double b) { for (int i = int(a * 100); i < int(b * 100); ++i) peaks[i] = 150; };
        speech(0, 6); speech(8, 16); speech(17, 30);
        QVector<MediaItem> media = {m};
        const QString text = kSentence;
        media[0].transcript = {{0.1, 5.9, text}, {8.1, 15.9, text}, {17.1, 22.0, text}, {22.3, 26.2, text}, {26.4, 29.9, text}};
        auto provider = [&](const QString& id) -> const QVector<quint8>* { return id == "talk" ? &peaks : nullptr; };

        const ai::VideoProfile profile = ai::analyzeProject(tl, media, provider);
        QVERIFY(profile.audio.valid && profile.hasTranscript);
        QVERIFY2(profile.wordsPerSecond > 2.2 && profile.wordsPerSecond < 3.5, qPrintable(QString::number(profile.wordsPerSecond)));
        const ai::EditPlan plan = ai::planEdit(profile, ai::EditStyle());
        const ai::EditResult r = ai::applyPlan(tl, plan, media, provider);

        // dead air: both pauses are gone (about 2.4 s), speech untouched
        QVERIFY2(r.silences == 2 && std::abs(r.oldDuration - r.newDuration - r.silenceSeconds) < 0.05, qPrintable(QString::number(r.silenceSeconds)));
        QVERIFY2(std::abs(tl.duration() - (30 - r.silenceSeconds)) < 0.05, qPrintable(QString::number(tl.duration())));
        QVERIFY(r.silenceSeconds > 2.0 && r.silenceSeconds < 3.0);
        // the long last take is split at a sentence start
        QVERIFY(r.splits >= 1);
        const int mt = ai::mainVideoTrack(tl);
        QCOMPARE(mt, 0);
        const QVector<Clip>& shots = tl.video[mt].clips;
        QVERIFY(shots.size() >= 4);
        QCOMPARE(r.shots, int(shots.size()));
        for (const Clip& c : shots) QVERIFY2(c.duration < 9.0, qPrintable(QString::number(c.duration)));
        // zoom: every other shot is closer, all of them creep in
        QVERIFY(r.zoomed >= 3);
        QVERIFY(std::abs(shots[0].effect("motion")->num("scale") - 100.0) < 1e-9);
        QVERIFY(shots[1].effect("motion")->num("scale") > 105.0 && shots[1].effect("motion")->num("scale") <= 150.0);
        QVERIFY(shots[0].effect("motion")->num("zoom_amount") > 0);
        QCOMPARE(shots[1].role, QString("motion"));
        // fades at both ends, tiny ones at every join of the sound
        QVERIFY(shots.first().effect("opacity")->num("fade_in") > 0.3);
        QVERIFY(shots.last().effect("opacity")->num("fade_out") > 0.5);
        const QVector<Clip>& sounds = tl.audio[0].clips;
        QVERIFY(sounds.size() >= 4);
        QVERIFY(sounds[1].effect("volume")->num("fade_in") > 0 && sounds[1].effect("volume")->num("fade_in") <= 0.02 + 1e-9);
        // captions: the sentences that are left, on top of the picture, marked as the assistant's
        QVERIFY(r.captions >= 5);
        int caps = 0;
        for (const Track& t : tl.video)
            for (const Clip& c : t.clips)
                if (c.role == "caption") { ++caps; QVERIFY(c.end() <= tl.duration() + 0.5); }
        QCOMPARE(caps, r.captions);
        // running the same plan again on the result never piles things up
        const ai::EditResult again = ai::applyPlan(tl, plan, media, provider);
        int caps2 = 0;
        for (const Track& t : tl.video)
            for (const Clip& c : t.clips)
                if (c.role == "caption") ++caps2;
        QVERIFY2(caps2 <= r.captions + 1, qPrintable(QString("%1 vs %2").arg(caps2).arg(r.captions)));
        QCOMPARE(again.silences, 0);

        // another wish on a fresh copy of the footage: no movement, no captions
        Timeline plain = Timeline::makeDefault();
        ops::placeClips(plain, ops::makeClips(m, 0, 30, 0), 0, 0, false);
        ai::EditStyle wish;
        wish.motion = false;
        wish.captions = false;
        const ai::EditResult r2 = ai::applyPlan(plain, ai::planEdit(profile, wish), media, provider);
        QCOMPARE(r2.zoomed, 0);
        QCOMPARE(r2.captions, 0);
        QCOMPARE(r2.splits, 0);
        for (const Clip& c : plain.video[0].clips) QVERIFY(c.effect("motion")->num("zoom_amount") == 0.0 && c.role.isEmpty());
        // the movements can be taken away again
        Timeline moved = tl;
        QVERIFY(ai::clearMotion(moved) > 0);
        for (const Clip& c : moved.video[0].clips) QVERIFY(c.effect("motion")->num("scale") == 100.0 && c.role != "motion");
        ai::clearFades(moved);
        QCOMPARE(moved.video[0].clips.first().effect("opacity")->num("fade_in"), 0.0);
    }

    void noisyFootageUsesTheSentencesToFindPauses() {
        const MediaItem m = talkMedia(20);
        Timeline tl = Timeline::makeDefault();
        ops::placeClips(tl, ops::makeClips(m, 0, 20, 0), 0, 0, false);
        QVector<quint8> peaks(2000, 22);                     // steady background noise, never below the silence threshold
        auto speech = [&](double a, double b, quint8 v) { for (int i = int(a * 100); i < int(b * 100); ++i) peaks[i] = v; };
        speech(0, 5, 150); speech(8, 12, 150);              // 5-8: only noise.  12-15: loud sound but no recognised words
        speech(12, 15, 100); speech(15, 20, 150);
        QVector<MediaItem> media = {m};
        const QString text = kSentence;
        media[0].transcript = {{0.1, 4.9, text}, {8.1, 11.9, text}, {15.1, 19.9, text}};
        auto provider = [&](const QString& id) -> const QVector<quint8>* { return id == "talk" ? &peaks : nullptr; };
        const ai::VideoProfile profile = ai::analyzeProject(tl, media, provider);
        const ai::EditPlan plan = ai::planEdit(profile, ai::EditStyle());
        QVERIFY2(!plan.cutByWaveform && plan.cutSpeechGaps, "noise this strong: pauses come from the sentences");
        ai::EditStyle only;
        only.motion = only.captions = only.fades = false;
        const ai::EditResult r = ai::applyPlan(tl, ai::planEdit(profile, only), media, provider);
        QCOMPARE(r.silences, 1);                            // the noise-only gap (5-8 s) is cut
        QCOMPARE(r.keptLoudGaps, 1);                        // the loud gap (12-15 s) might be missed speech: kept
        QVERIFY2(std::abs(tl.duration() - (20 - r.silenceSeconds)) < 0.05, qPrintable(QString::number(tl.duration())));
        QVERIFY(r.silenceSeconds > 2.0 && r.silenceSeconds < 3.0);
    }

    // ------------------------------------------------------------------ audio clean-up
    void cleanUpIsChosenFromTheMeasuredSound() {
        auto stats = [](double noise, double speech) { ai::AudioStats s; s.valid = true; s.noiseFloorDb = noise; s.speechDb = speech; return s; };
        QCOMPARE(ai::chooseCleanup(stats(-55, -8)), ai::Cleanup::None);       // clean and loud: leave it alone
        QCOMPARE(ai::chooseCleanup(stats(-60, -30)), ai::Cleanup::Light);     // quiet recording: level it
        QCOMPARE(ai::chooseCleanup(stats(-32, -12)), ai::Cleanup::Strong);    // speech only 20 dB above the noise
        QCOMPARE(ai::chooseCleanup(stats(-28, -15)), ai::Cleanup::Strong);
        QCOMPARE(ai::chooseCleanup(ai::AudioStats()), ai::Cleanup::Light);    // unknown: the gentle one
        QCOMPARE(ai::cleanupFilter(ai::Cleanup::None), QString());
        QVERIFY(ai::cleanupFilter(ai::Cleanup::Light).contains("dynaudnorm"));
        QVERIFY(ai::cleanupFilter(ai::Cleanup::Strong).contains("afftdn"));
        QCOMPARE(ai::cleanupFromName("STRONG", ai::Cleanup::None), ai::Cleanup::Strong);
        QCOMPARE(ai::cleanupFromName("whatever", ai::Cleanup::Light), ai::Cleanup::Light);
        // measured from a waveform: 15 % of the time at the noise level, the loud 10 % are the speech
        QVector<quint8> peaks(1000, 6);
        for (int i = 0; i < 400; ++i) peaks[i] = 140;
        const ai::AudioStats m = ai::measureAudio(peaks);
        QVERIFY(m.valid);
        QVERIFY2(m.dynamicDb() > 25, qPrintable(QString::number(m.dynamicDb())));
        QVERIFY(!ai::measureAudio(QVector<quint8>(20, 100)).valid);           // under a second: nothing to measure
    }

    // ------------------------------------------------------------------ captions
    void captionsKeepTheirLookWhenTheTextChanges() {
        Timeline tl = Timeline::makeDefault();
        ai::CaptionStyle style;
        style.color = "#ffd60a";
        style.size = 70;
        style.positionY = 30;
        QVector<ai::TimelineSegment> segs = {{0, 3, QString::fromUtf8("الجملة الاولى")}, {3, 6, QString("a long sentence that has to be split in two parts for the screen")}};
        const int track = ai::addCaptions(tl, segs, style);
        QVERIFY(track >= 0);
        int caps = 0;
        for (const Clip& c : tl.video[track].clips) { QCOMPARE(c.role, QString("caption")); ++caps; }
        QVERIFY(caps >= 2);
        QCOMPARE(tl.video[track].clips[0].effect("text")->text("color"), QString("#ffd60a"));
        QCOMPARE(tl.video[track].clips[0].effect("motion")->num("position_y"), 30.0);

        // restyle: only what is asked changes
        QCOMPARE(ai::restyleCaptions(tl, "#34c759", 0, qQNaN()), caps);
        QCOMPARE(tl.video[track].clips[0].effect("text")->text("color"), QString("#34c759"));
        QCOMPARE(tl.video[track].clips[0].effect("text")->num("size"), 70.0);
        QCOMPARE(tl.video[track].clips[0].effect("motion")->num("position_y"), 30.0);
        ai::restyleCaptions(tl, QString(), 90, -30);
        QCOMPARE(tl.video[track].clips[0].effect("text")->num("size"), 90.0);
        QCOMPARE(tl.video[track].clips[0].effect("motion")->num("position_y"), -30.0);

        // new text for the first sentence: same colour / size / place, other sentences untouched
        QVERIFY(ai::retextCaptions(tl, 0, 3, QString::fromUtf8("جملة جديدة")) >= 1);
        QString first;
        for (const Clip& c : tl.video[track].clips)
            if (c.start < 1) first = c.effect("text")->text("text");
        QCOMPARE(first, QString::fromUtf8("جملة جديدة"));
        for (const Clip& c : tl.video[track].clips) QCOMPARE(c.effect("text")->text("color"), QString("#34c759"));
        QCOMPARE(ai::retextCaptions(tl, 50, 60, "nothing here"), 0);

        // word replacement in captions ignores spelling variants
        QCOMPARE(ai::replaceInCaptions(tl, QString::fromUtf8("جمله"), QString::fromUtf8("عبارة")), 1);
        QVERIFY(tl.video[track].clips[0].effect("text")->text("text").contains(QString::fromUtf8("عبارة")));
        // a title written by the user is never touched
        Clip mine = ops::makeTitle(10, 2, QString::fromUtf8("عنوان"));
        tl.video[0].clips.push_back(mine);
        QCOMPARE(ai::removeCaptions(tl), caps);
        QCOMPARE(tl.video[0].clips.last().effect("text")->text("text"), QString::fromUtf8("عنوان"));
    }

    // ------------------------------------------------------------------ motion
    void zoomAndDriftAnimateOverTheClip() {
        const QString red = dir.filePath("red4.mp4");
        QVERIFY(runFfmpeg({"-f", "lavfi", "-i", "color=c=0xff0000:s=320x180:r=30:d=4", "-c:v", "libx264", "-pix_fmt", "yuv420p", red}));
        Project p;
        const MediaItem item = *p.mediaById(p.importFile(red));
        Timeline tl = Timeline::makeDefault();
        tl.width = 320;
        tl.height = 180;
        ops::placeClips(tl, ops::makeClips(item, 0, 4, 0), 0, 0, false);
        EffectInstance* mo = tl.video[0].clips[0].effect("motion");
        QVERIFY(mo);
        mo->set("scale", 50.0);
        mo->set("zoom_amount", 100.0);       // 50 % -> 100 % over the clip
        DecoderPool pool;
        Renderer rend(&pool);
        auto width = [&](double t) { const Blob b = blobOf(rend.render(tl, t, QSize(320, 180))); return b.right - b.left + 1; };
        QVERIFY2(std::abs(width(0.02) - 160) <= 4, qPrintable(QString::number(width(0.02))));
        QVERIFY2(std::abs(width(2.0) - 240) <= 5, qPrintable(QString::number(width(2.0))));
        QVERIFY2(width(3.97) >= 312, qPrintable(QString::number(width(3.97))));
        // no movement stays exactly as before
        mo->set("zoom_amount", 0.0);
        QVERIFY(std::abs(width(0.02) - 160) <= 4 && std::abs(width(3.97) - 160) <= 4);
        // drifting sideways: the centre of the picture moves by 25 % of the width (80 px) during the clip
        mo->set("pan_x", 25.0);
        const double c0 = blobOf(rend.render(tl, 0.02, QSize(320, 180))).cx;
        const double c1 = blobOf(rend.render(tl, 3.97, QSize(320, 180))).cx;
        QVERIFY2(std::abs((c1 - c0) - 80) <= 4, qPrintable(QString("%1 -> %2").arg(c0).arg(c1)));
        // titles move the same way
        Clip title = ops::makeTitle(0, 2, "HELLO");
        title.effect("text")->set("size", 40.0);
        title.effect("motion")->set("zoom_amount", 100.0);
        Timeline tt = Timeline::makeDefault();
        tt.width = 320;
        tt.height = 180;
        tt.video[0].clips.push_back(title);
        const Blob t0 = blobOf(rend.render(tt, 0.02, QSize(320, 180)));
        const Blob t1 = blobOf(rend.render(tt, 1.95, QSize(320, 180)));
        QVERIFY2((t1.right - t1.left) > (t0.right - t0.left) * 1.6, qPrintable(QString("%1 -> %2").arg(t0.right - t0.left).arg(t1.right - t1.left)));
    }

    void movementSurvivesSavingAndOldProjectsStillLoad() {
        Project p;
        Timeline tl = Timeline::makeDefault();
        Clip c = ops::makeTitle(0, 2, "x");
        c.role = "caption";
        c.effect("motion")->set("zoom_amount", 12.5);
        tl.video[0].clips.push_back(c);
        p.applyTimeline(tl);
        const QString file = dir.filePath("moving.hrproj");
        QVERIFY(p.save(file));
        Project q;
        QVERIFY(q.load(file));
        const Clip& r = q.timeline().video[0].clips[0];
        QCOMPARE(r.role, QString("caption"));
        QVERIFY(std::abs(r.effect("motion")->num("zoom_amount") - 12.5) < 1e-9);
        // a clip without the new parameters (a project saved by an older version) gets the neutral defaults
        EffectInstance old;
        old.type = "motion";
        old.params.insert("scale", 120.0);
        QCOMPARE(old.num("zoom_amount"), 0.0);
        QCOMPARE(old.num("pan_x"), 0.0);
    }
};

QTEST_MAIN(AiTests)
#include "test_ai.moc"
