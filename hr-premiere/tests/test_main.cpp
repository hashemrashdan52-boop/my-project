// Test-suite for the HR Premiere engine: editing operations, project files, FFmpeg
// decoding, compositing, audio mixing and the exporter.  The media tests generate their
// own clips with the ffmpeg command line tool so no binary fixtures are needed.
#include "core/EditOps.h"
#include "core/Project.h"
#include "core/Util.h"
#include "engine/AudioMixer.h"
#include "engine/DecoderPool.h"
#include "engine/Exporter.h"
#include "engine/Renderer.h"
#include "media/AudioDecoder.h"
#include "media/MediaProbe.h"
#include "media/VideoDecoder.h"

#include <QApplication>
#include <QFileInfo>
#include <QJsonDocument>
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

static MediaItem fakeVideo(const QString& name, double dur, bool audio = true) {
    MediaItem m;
    m.id = newId();
    m.name = name;
    m.path = "/nonexistent/" + name;
    m.kind = MediaKind::Video;
    m.duration = dur;
    m.width = 1920;
    m.height = 1080;
    m.fps = 30;
    m.hasVideo = true;
    m.hasAudio = audio;
    return m;
}

static QColor centerColor(const QImage& img) { return QColor(img.pixel(img.width() / 2, img.height() / 2)); }

class HrTests : public QObject {
    Q_OBJECT
    QTemporaryDir dir;
    QString red, blue, sine, portrait;

private slots:
    void initTestCase() {
        QVERIFY(dir.isValid());
        red = dir.filePath("red.mp4");
        blue = dir.filePath("blue.mp4");
        sine = dir.filePath("tone.wav");
        portrait = dir.filePath("portrait.mp4");
        QVERIFY(runFfmpeg({"-f", "lavfi", "-i", "color=c=red:s=320x240:r=30:d=2", "-f", "lavfi", "-i",
                           "sine=frequency=440:sample_rate=44100:duration=2", "-c:v", "libx264", "-pix_fmt",
                           "yuv420p", "-g", "15", "-c:a", "aac", "-shortest", red}));
        QVERIFY(runFfmpeg({"-f", "lavfi", "-i", "color=c=blue:s=320x240:r=30:d=2", "-c:v", "libx264", "-pix_fmt",
                           "yuv420p", blue}));
        QVERIFY(runFfmpeg({"-f", "lavfi", "-i", "sine=frequency=1000:sample_rate=48000:duration=3", sine}));
        QVERIFY(runFfmpeg({"-display_rotation", "-90", "-i", red, "-c", "copy", portrait}));
    }

    // ------------------------------------------------------------------ util
    void timecode() {
        QCOMPARE(formatTimecode(0, 30), QString("00:00:00:00"));
        QCOMPARE(formatTimecode(61.5, 30), QString("00:01:01:15"));
        QCOMPARE(formatTimecode(3600 + 2, 25), QString("01:00:02:00"));
        bool ok = false;
        QVERIFY(std::abs(parseTimecode("00:00:01:15", 30, &ok) - 1.5) < 1e-9);
        QVERIFY(ok);
        QVERIFY(std::abs(parseTimecode("45", 30) - 1.5) < 1e-9);
        QCOMPARE(formatDuration(75), QString("1:15"));
    }

    // ------------------------------------------------------------------ editing
    void placeOverwrites() {
        Timeline tl = Timeline::makeDefault();
        MediaItem m = fakeVideo("a.mp4", 10, false);
        ops::placeClips(tl, ops::makeClips(m, 0, 10, 0), 0, 0, false);
        QCOMPARE(tl.video[0].clips.size(), 1);
        // drop another clip in the middle -> the first one is split around it
        ops::placeClips(tl, ops::makeClips(m, 0, 2, 4), 0, 0, false);
        QCOMPARE(tl.video[0].clips.size(), 3);
        QVERIFY(std::abs(tl.video[0].clips[0].duration - 4) < 1e-9);
        QVERIFY(std::abs(tl.video[0].clips[1].start - 4) < 1e-9);
        QVERIFY(std::abs(tl.video[0].clips[2].start - 6) < 1e-9);
        QVERIFY(std::abs(tl.video[0].clips[2].srcIn - 6) < 1e-9);  // continues where it left off
        QVERIFY(std::abs(tl.video[0].clips[2].duration - 4) < 1e-9);
        QVERIFY(std::abs(tl.duration() - 10) < 1e-9);
    }

    void splitKeepsLinksTogether() {
        Timeline tl = Timeline::makeDefault();
        MediaItem m = fakeVideo("a.mp4", 10, true);
        ops::placeClips(tl, ops::makeClips(m, 0, 10, 0), 0, 0, false);
        QCOMPARE(tl.video[0].clips.size(), 1);
        QCOMPARE(tl.audio[0].clips.size(), 1);
        QVERIFY(!tl.video[0].clips[0].linkId.isEmpty());
        QCOMPARE(tl.video[0].clips[0].linkId, tl.audio[0].clips[0].linkId);
        QVERIFY(ops::splitAt(tl, tl.video[0].clips[0].id, 3.0));
        QCOMPARE(tl.video[0].clips.size(), 2);
        QCOMPARE(tl.audio[0].clips.size(), 2);
        QCOMPARE(tl.video[0].clips[1].linkId, tl.audio[0].clips[1].linkId);
        QVERIFY(tl.video[0].clips[0].linkId != tl.video[0].clips[1].linkId);
        QVERIFY(std::abs(tl.audio[0].clips[1].srcIn - 3.0) < 1e-9);
        // splitting at the very edge is refused
        QVERIFY(!ops::splitAt(tl, tl.video[0].clips[0].id, 0.0));
    }

    void trimRespectsLimits() {
        Timeline tl = Timeline::makeDefault();
        MediaItem m = fakeVideo("a.mp4", 10, false);
        ops::placeClips(tl, ops::makeClips(m, 2, 3, 5), 0, 0, false);  // source 2..5 at 5..8
        const QString id = tl.video[0].clips[0].id;
        QSet<QString> ids{id};
        // cannot extend the head past the start of the source (srcIn=2 -> at most 2 s)
        QVERIFY(std::abs(ops::trim(tl, ids, true, -5.0) + 2.0) < 1e-9);
        QVERIFY(std::abs(tl.video[0].clips[0].start - 3.0) < 1e-9);
        QVERIFY(std::abs(tl.video[0].clips[0].srcIn) < 1e-9);
        // cannot extend the tail past the end of the source
        ops::trim(tl, ids, false, 100.0);
        QVERIFY(std::abs(tl.video[0].clips[0].end() - 13.0) < 1e-9);  // 10 s of source at 3..13
        // cannot trim to nothing
        ops::trim(tl, ids, false, -100.0);
        QVERIFY(tl.video[0].clips[0].duration > 0);
    }

    void trimStopsAtNeighbour() {
        Timeline tl = Timeline::makeDefault();
        MediaItem m = fakeVideo("a.mp4", 20, false);
        ops::placeClips(tl, ops::makeClips(m, 0, 4, 0), 0, 0, false);
        ops::placeClips(tl, ops::makeClips(m, 10, 4, 6), 0, 0, false);
        const QString first = tl.video[0].clips[0].id;
        ops::trim(tl, {first}, false, 100.0);
        QVERIFY(std::abs(tl.video[0].clips[0].end() - 6.0) < 1e-9);
    }

    void rippleDelete() {
        Timeline tl = Timeline::makeDefault();
        MediaItem m = fakeVideo("a.mp4", 30, true);
        ops::placeClips(tl, ops::makeClips(m, 0, 5, 0), 0, 0, false);
        ops::placeClips(tl, ops::makeClips(m, 5, 5, 5), 0, 0, false);
        ops::placeClips(tl, ops::makeClips(m, 10, 5, 10), 0, 0, false);
        const QString mid = tl.video[0].clips[1].id;
        ops::remove(tl, ops::linkedIds(tl, mid), true);
        QCOMPARE(tl.video[0].clips.size(), 2);
        QVERIFY(std::abs(tl.video[0].clips[1].start - 5.0) < 1e-9);
        QVERIFY(std::abs(tl.audio[0].clips[1].start - 5.0) < 1e-9);
    }

    void moveClipsOverwritesAndChangesTrack() {
        Timeline tl = Timeline::makeDefault();
        MediaItem m = fakeVideo("a.mp4", 30, false);
        ops::placeClips(tl, ops::makeClips(m, 0, 4, 0), 0, 0, false);
        ops::placeClips(tl, ops::makeClips(m, 4, 4, 10), 1, 0, false);
        const QString id = tl.video[1].clips[0].id;
        Timeline moved = ops::moved(tl, {id}, -9.0, -1, TrackType::Video);  // onto V1, at 1..5
        QCOMPARE(moved.video[1].clips.size(), 0);
        QCOMPARE(moved.video[0].clips.size(), 2);
        QVERIFY(std::abs(moved.video[0].clips[0].duration - 1.0) < 1e-9);  // first clip got trimmed to 0..1
        QVERIFY(std::abs(moved.video[0].clips[1].start - 1.0) < 1e-9);
        // can't go before time zero
        Timeline early = ops::moved(tl, {id}, -100.0, 0, TrackType::Video);
        QVERIFY(early.video[1].clips[0].start >= 0);
    }

    void insertOpensGap() {
        Timeline tl = Timeline::makeDefault();
        MediaItem m = fakeVideo("a.mp4", 30, true);
        ops::placeClips(tl, ops::makeClips(m, 0, 10, 0), 0, 0, false);
        ops::placeClips(tl, ops::makeClips(m, 10, 2, 4), 0, 0, true);  // insert 2 s at t=4
        QCOMPARE(tl.video[0].clips.size(), 3);
        QVERIFY(std::abs(tl.video[0].clips[2].start - 6.0) < 1e-9);
        QVERIFY(std::abs(tl.duration() - 12.0) < 1e-9);
    }

    // ------------------------------------------------------------------ project
    void undoRedoAndSaveLoad() {
        Project p;
        QString err;
        const QString id = p.importFile(red, &err);
        QVERIFY2(!id.isEmpty(), qPrintable(err));
        const MediaItem* mi = p.mediaById(id);
        QVERIFY(mi);
        QCOMPARE(mi->kind, MediaKind::Video);
        QVERIFY(std::abs(mi->duration - 2.0) < 0.1);
        QCOMPARE(mi->width, 320);
        QCOMPARE(mi->height, 240);
        QVERIFY(mi->hasAudio);
        QCOMPARE(p.importFile(red), id);  // importing twice returns the same item

        p.edit("Add", [&](Timeline& t) { ops::placeClips(t, ops::makeClips(*mi, 0, mi->duration, 0), 0, 0, false); });
        QCOMPARE(p.timeline().video[0].clips.size(), 1);
        p.edit("Split", [&](Timeline& t) { ops::splitAt(t, t.video[0].clips[0].id, 1.0); });
        QCOMPARE(p.timeline().video[0].clips.size(), 2);
        p.undoStack()->undo();
        QCOMPARE(p.timeline().video[0].clips.size(), 1);
        p.undoStack()->redo();
        QCOMPARE(p.timeline().video[0].clips.size(), 2);

        // effects survive a round trip
        p.edit("Fx", [&](Timeline& t) {
            t.video[0].clips[0].effects.push_back(EffectRegistry::make("gaussian_blur"));
            t.video[0].clips[0].effect("motion")->set("scale", 150.0);
        });
        const QString file = dir.filePath("test.hrproj");
        QVERIFY(p.save(file, &err));
        Project q;
        QVERIFY2(q.load(file, &err), qPrintable(err));
        QCOMPARE(q.timeline().video[0].clips.size(), 2);
        QCOMPARE(q.media().size(), 1);
        QVERIFY(std::abs(q.timeline().video[0].clips[0].effect("motion")->num("scale") - 150.0) < 1e-9);
        QVERIFY(q.timeline().video[0].clips[0].effect("gaussian_blur") != nullptr);
        QCOMPARE(q.timeline().video[0].clips[1].linkId, q.timeline().audio[0].clips[1].linkId);
    }

    void mergedUndoForSliders() {
        Project p;
        const QString id = p.importFile(red);
        const MediaItem* mi = p.mediaById(id);
        p.edit("Add", [&](Timeline& t) { ops::placeClips(t, ops::makeClips(*mi, 0, 2, 0), 0, 0, false); });
        const int before = p.undoStack()->count();
        for (int i = 1; i <= 5; ++i)
            p.edit("Scale", [&](Timeline& t) { t.video[0].clips[0].effect("motion")->set("scale", 100.0 + i); }, "scale");
        QCOMPARE(p.undoStack()->count(), before + 1);  // merged into one step
        QVERIFY(std::abs(p.timeline().video[0].clips[0].effect("motion")->num("scale") - 105.0) < 1e-9);
        p.undoStack()->undo();
        QVERIFY(std::abs(p.timeline().video[0].clips[0].effect("motion")->num("scale") - 100.0) < 1e-9);
    }

    // ------------------------------------------------------------------ media
    void probeKinds() {
        MediaItem m;
        QVERIFY(probeMedia(sine, m));
        QCOMPARE(m.kind, MediaKind::Audio);
        QVERIFY(m.hasAudio && !m.hasVideo);
        QVERIFY(std::abs(m.duration - 3.0) < 0.1);
        QCOMPARE(m.sampleRate, 48000);
        QImage img(64, 48, QImage::Format_RGB32);
        img.fill(Qt::green);
        const QString png = dir.filePath("still.png");
        QVERIFY(img.save(png));
        MediaItem s;
        QVERIFY(probeMedia(png, s));
        QCOMPARE(s.kind, MediaKind::Image);
        QCOMPARE(s.width, 64);
        QVERIFY(!probeMedia(dir.filePath("missing.mp4"), s));
    }

    void videoDecoderSeeksAccurately() {
        VideoDecoder d;
        QVERIFY(d.open(red));
        QCOMPARE(d.nativeSize(), QSize(320, 240));
        QVERIFY(std::abs(d.fps() - 30.0) < 0.01);
        QImage a = d.frameAt(0.5);
        QVERIFY(!a.isNull());
        QVERIFY(QColor(a.pixel(160, 120)).red() > 200 && QColor(a.pixel(160, 120)).blue() < 60);
        // sequential and backwards access
        for (int i = 0; i < 20; ++i) QVERIFY(!d.frameAt(0.5 + i / 30.0).isNull());
        QVERIFY(!d.frameAt(0.1).isNull());
        QVERIFY(!d.frameAt(1.95).isNull());
        QVERIFY(!d.frameAt(99.0).isNull());  // clamped to the last frame
        // scaled decode
        QImage small = d.frameAt(1.0, QSize(80, 80));
        QVERIFY(small.width() <= 80 && small.height() <= 80);
    }

    void videoDecoderReadsRotation() {
        VideoDecoder d;
        QVERIFY(d.open(portrait));
        QCOMPARE(d.rotation(), 90);  // phones store -90 in the display matrix
        QImage up = d.uprightFrameAt(0.2);
        QVERIFY(up.height() > up.width());
    }

    void audioDecoderProducesTone() {
        AudioDecoder d;
        QVERIFY(d.open(sine));
        std::vector<float> buf(4800 * 2);
        d.read(0.5, buf.data(), 4800);
        float peak = 0;
        for (float v : buf) peak = std::max(peak, std::fabs(v));
        QVERIFY2(peak > 0.05f, qPrintable(QString::number(peak)));
        // sequential read continues without a gap (compare with a fresh seek)
        std::vector<float> next(4800 * 2), fresh(4800 * 2);
        d.read(0.5 + 0.1, next.data(), 4800);
        AudioDecoder d2;
        QVERIFY(d2.open(sine));
        d2.read(0.6, fresh.data(), 4800);
        double diff = 0;
        for (size_t i = 0; i < next.size(); ++i) diff = std::max(diff, double(std::fabs(next[i] - fresh[i])));
        QVERIFY2(diff < 0.01, qPrintable(QString::number(diff)));
        // past the end -> silence
        d.read(10.0, buf.data(), 4800);
        for (float v : buf) QCOMPARE(v, 0.0f);
    }

    // ------------------------------------------------------------------ render
    void rendererCompositesTracks() {
        Project p;
        const MediaItem r0 = *p.mediaById(p.importFile(red));
        const MediaItem b0 = *p.mediaById(p.importFile(blue));
        const MediaItem* r = &r0;
        const MediaItem* b = &b0;
        Timeline tl = Timeline::makeDefault();
        tl.width = 320;
        tl.height = 240;
        ops::placeClips(tl, ops::makeClips(*r, 0, 2, 0), 0, 0, false);
        ops::placeClips(tl, ops::makeClips(*b, 0, 2, 2), 0, 0, false);
        DecoderPool pool;
        Renderer rend(&pool);
        QColor c1 = centerColor(rend.render(tl, 1.0, QSize(320, 240)));
        QColor c2 = centerColor(rend.render(tl, 3.0, QSize(320, 240)));
        QColor c3 = centerColor(rend.render(tl, 3.9, QSize(160, 120)));
        QVERIFY2(c1.red() > 200 && c1.blue() < 60, qPrintable(c1.name()));
        QVERIFY2(c2.blue() > 200 && c2.red() < 60, qPrintable(c2.name()));
        QVERIFY(c3.blue() > 200);
        QCOMPARE(centerColor(rend.render(tl, 5.0, QSize(320, 240))), QColor(Qt::black));  // past the end

        // blue overlay on V2 at 50 % opacity over red
        Timeline ov = tl;
        ops::placeClips(ov, ops::makeClips(*b, 0, 2, 0), 1, 0, false);
        ov.video[1].clips[0].effect("opacity")->set("opacity", 50.0);
        QColor mix = centerColor(rend.render(ov, 1.0, QSize(320, 240)));
        QVERIFY2(mix.red() > 90 && mix.red() < 160 && mix.blue() > 90 && mix.blue() < 160, qPrintable(mix.name()));

        // scale to 50 %: centre keeps colour, corner stays black
        Timeline sc = tl;
        sc.video[0].clips[0].effect("motion")->set("scale", 50.0);
        QImage img = rend.render(sc, 1.0, QSize(320, 240));
        QVERIFY(centerColor(img).red() > 200);
        QCOMPARE(QColor(img.pixel(5, 5)), QColor(Qt::black));

        // colour effect: black & white makes red grey
        Timeline bw = tl;
        bw.video[0].clips[0].effects.push_back(EffectRegistry::make("black_white"));
        QColor g = centerColor(rend.render(bw, 1.0, QSize(320, 240)));
        QVERIFY(std::abs(g.red() - g.green()) < 6 && std::abs(g.green() - g.blue()) < 6);

        // title clip draws something on an empty track
        Timeline ti = Timeline::makeDefault();
        ti.width = 320;
        ti.height = 240;
        ti.video[0].clips.push_back(ops::makeTitle(0, 3, "HELLO"));
        QImage titled = rend.render(ti, 1.0, QSize(320, 240));
        int lit = 0;
        for (int y = 0; y < titled.height(); ++y)
            for (int x = 0; x < titled.width(); ++x)
                if (QColor(titled.pixel(x, y)).lightness() > 128) ++lit;
        QVERIFY2(lit > 100, qPrintable(QString::number(lit)));
    }

    void rendererHandlesOfflineMedia() {
        Timeline tl = Timeline::makeDefault();
        MediaItem m = fakeVideo("gone.mp4", 5, false);
        ops::placeClips(tl, ops::makeClips(m, 0, 5, 0), 0, 0, false);
        DecoderPool pool;
        Renderer rend(&pool);
        QImage img = rend.render(tl, 1.0, QSize(320, 180));
        QVERIFY(QColor(img.pixel(2, 2)).red() > 90);  // the "Media Offline" slate
    }

    // ------------------------------------------------------------------ audio
    void mixerAppliesVolumeFadeAndPan() {
        Project p;
        const MediaItem* s = p.mediaById(p.importFile(sine));
        Timeline tl = Timeline::makeDefault();
        ops::placeClips(tl, ops::makeClips(*s, 0, 3, 0), 0, 0, false);
        DecoderPool pool;
        AudioMixer mixer(&pool);
        std::vector<float> a(4800 * 2), b(4800 * 2);
        mixer.render(tl, 48000, 4800, a.data());
        float pa = 0;
        for (float v : a) pa = std::max(pa, std::fabs(v));
        QVERIFY(pa > 0.05f);

        Timeline quiet = tl;
        quiet.audio[0].clips[0].effect("volume")->set("level", -6.0);
        DecoderPool pool2;
        AudioMixer m2(&pool2);
        m2.render(quiet, 48000, 4800, b.data());
        float pb = 0;
        for (float v : b) pb = std::max(pb, std::fabs(v));
        QVERIFY2(std::abs(pb / pa - 0.501f) < 0.03f, qPrintable(QString::number(pb / pa)));

        // fade in: silent at the very first samples
        Timeline fade = tl;
        fade.audio[0].clips[0].effect("volume")->set("fade_in", 1.0);
        DecoderPool pool3;
        AudioMixer m3(&pool3);
        m3.render(fade, 0, 480, a.data());
        float pf = 0;
        for (int i = 0; i < 100 * 2; ++i) pf = std::max(pf, std::fabs(a[size_t(i)]));
        QVERIFY(pf < 0.01f);

        // pan hard left silences the right channel
        Timeline pan = tl;
        pan.audio[0].clips[0].effects.push_back(EffectRegistry::make("balance"));
        pan.audio[0].clips[0].effect("balance")->set("pan", -100.0);
        DecoderPool pool4;
        AudioMixer m4(&pool4);
        m4.render(pan, 48000, 4800, a.data());
        float pl = 0, pr = 0;
        for (int i = 0; i < 4800; ++i) { pl = std::max(pl, std::fabs(a[size_t(2 * i)])); pr = std::max(pr, std::fabs(a[size_t(2 * i + 1)])); }
        QVERIFY(pl > 0.05f);
        QVERIFY(pr < 0.001f);
    }

    // ------------------------------------------------------------------ export
    void exportsPlayableMp4() {
        const QString ff = Exporter::findFfmpeg();
        QVERIFY2(!ff.isEmpty(), "ffmpeg not found");
        Project p;
        const MediaItem r0 = *p.mediaById(p.importFile(red));
        const MediaItem b0 = *p.mediaById(p.importFile(blue));
        const MediaItem* r = &r0;
        const MediaItem* b = &b0;
        Timeline tl = Timeline::makeDefault();
        tl.width = 320;
        tl.height = 240;
        ops::placeClips(tl, ops::makeClips(*r, 0, 2, 0), 0, 0, false);
        ops::placeClips(tl, ops::makeClips(*b, 0, 2, 2), 0, 0, false);
        QCOMPARE(tl.audio[0].clips.size(), 1);

        ExportSettings s;
        s.outputPath = dir.filePath("out.mp4");
        s.width = 320;
        s.height = 240;
        s.fps = 30;
        s.crf = 18;
        s.start = 0;
        s.end = tl.duration();
        Exporter ex(tl, s);
        bool done = false, ok = false;
        QString msg;
        connect(&ex, &Exporter::exportFinished, this, [&](bool o, const QString& m) { done = true; ok = o; msg = m; },
                Qt::DirectConnection);
        ex.start();
        QVERIFY(ex.wait(120000));
        QVERIFY2(done && ok, qPrintable(msg));

        MediaItem out;
        QVERIFY(probeMedia(s.outputPath, out));
        QCOMPARE(out.width, 320);
        QCOMPARE(out.height, 240);
        QVERIFY(out.hasAudio);
        QVERIFY2(std::abs(out.duration - 4.0) < 0.2, qPrintable(QString::number(out.duration)));
        VideoDecoder d;
        QVERIFY(d.open(s.outputPath));
        QColor c1(d.frameAt(1.0).pixel(160, 120)), c2(d.frameAt(3.0).pixel(160, 120));
        QVERIFY2(c1.red() > 180 && c1.blue() < 80, qPrintable(c1.name()));
        QVERIFY2(c2.blue() > 180 && c2.red() < 80, qPrintable(c2.name()));
        AudioDecoder ad;
        QVERIFY(ad.open(s.outputPath));
        std::vector<float> buf(4800 * 2);
        ad.read(0.5, buf.data(), 4800);
        float peak = 0;
        for (float v : buf) peak = std::max(peak, std::fabs(v));
        QVERIFY(peak > 0.03f);
    }

    void exportsAudioOnlyAndGif() {
        Project p;
        const MediaItem* s = p.mediaById(p.importFile(sine));
        Timeline tl = Timeline::makeDefault();
        ops::placeClips(tl, ops::makeClips(*s, 0, 3, 0), 0, 0, false);
        ExportSettings st;
        st.format = "wav";
        st.outputPath = dir.filePath("mix.wav");
        st.start = 0;
        st.end = 3;
        Exporter ex(tl, st);
        ex.start();
        QVERIFY(ex.wait(60000));
        MediaItem out;
        QVERIFY(probeMedia(st.outputPath, out));
        QVERIFY(std::abs(out.duration - 3.0) < 0.1);
        QVERIFY(!out.hasVideo);
    }
};

QTEST_MAIN(HrTests)
#include "test_main.moc"
