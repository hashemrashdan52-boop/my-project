// Tests for the AI assistant's logic (no window needed): Arabic text matching, transcript edits, motion,
// transcript quality, audio clean-up, the smart edit planner and the chat command parser.
#include "ai/Cleanup.h"
#include "ai/Silence.h"
#include "ai/Transcript.h"
#include "core/EditOps.h"
#include "core/Project.h"
#include "core/TextUtil.h"
#include "engine/DecoderPool.h"
#include "engine/Exporter.h"
#include "engine/Renderer.h"

#include <QProcess>
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

class AiTests : public QObject {
    Q_OBJECT
    QTemporaryDir dir;

private slots:
    void initTestCase() { QVERIFY(dir.isValid()); }

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
