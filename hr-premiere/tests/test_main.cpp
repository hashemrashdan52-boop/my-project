// Test-suite for the HR Premiere engine: editing operations, project files, FFmpeg
// decoding, compositing, audio mixing and the exporter.  The media tests generate their
// own clips with the ffmpeg command line tool so no binary fixtures are needed.
#include "ai/Scenes.h"
#include "ai/Silence.h"
#include "ai/Transcript.h"
#include "ai/Whisper.h"
#include "core/EditOps.h"
#include "core/Project.h"
#include "core/Util.h"
#include "engine/AudioMixer.h"
#include "engine/DecoderPool.h"
#include "engine/Exporter.h"
#include "engine/Renderer.h"
#include "media/AudioDecoder.h"
#include "media/MediaAssets.h"
#include "media/MediaProbe.h"
#include "media/VideoDecoder.h"

#include <QApplication>
#include <QCoreApplication>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QEventLoop>
#include <QTimer>
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

// A minimal HTTP server for the model download tests: "/ok" -> 300000 bytes, "/small" -> 10 bytes, anything else 404.
class TinyHttpServer : public QObject {
public:
    explicit TinyHttpServer(QObject* parent = nullptr) : QObject(parent) {
        listen();
    }
    quint16 port() const { return m_server.serverPort(); }
    QByteArray okBody() const { return QByteArray(300000, 'x'); }

private:
    void listen() {
        m_server.listen(QHostAddress::LocalHost);
        connect(&m_server, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket* sock = m_server.nextPendingConnection()) {
                connect(sock, &QTcpSocket::readyRead, sock, [this, sock] {
                    const QByteArray req = sock->readAll();
                    const QString line = QString::fromLatin1(req.left(req.indexOf("\r\n")));
                    QByteArray body;
                    QByteArray status = "200 OK";
                    if (line.contains("/ok")) body = okBody();
                    else if (line.contains("/small")) body = QByteArray(10, 'y');
                    else { status = "404 Not Found"; body = "nope"; }
                    sock->write("HTTP/1.1 " + status + "\r\nContent-Length: " + QByteArray::number(body.size()) +
                                "\r\nConnection: close\r\n\r\n" + body);
                    sock->disconnectFromHost();
                });
            }
        });
    }
    QTcpServer m_server;
};

struct FakeFfmpegEnv {
    QTemporaryDir aiDir;
    QByteArray oldFfmpeg;
    bool had;
    FakeFfmpegEnv() {
        had = qEnvironmentVariableIsSet("HR_FFMPEG");
        oldFfmpeg = qgetenv("HR_FFMPEG");
#ifdef Q_OS_WIN
        const QString fake = QCoreApplication::applicationDirPath() + "/fake_ffmpeg.exe";
#else
        const QString fake = QCoreApplication::applicationDirPath() + "/fake_ffmpeg";
#endif
        qputenv("HR_FFMPEG", fake.toUtf8());
        qputenv("HR_AI_DIR", aiDir.path().toUtf8());
        QFile m(aiDir.filePath("ggml-test.bin"));
        m.open(QIODevice::WriteOnly);
        m.write(QByteArray(2000, 'm'));
        QFile v(aiDir.filePath("ggml-silero-v5.1.2.bin"));
        v.open(QIODevice::WriteOnly);
        v.write(QByteArray(500 * 1024, 'v'));
    }
    ~FakeFfmpegEnv() {
        qunsetenv("HR_AI_DIR");
        qunsetenv("FAKE_FFMPEG_FAIL");
        qunsetenv("FAKE_FFMPEG_FAIL_VAD");
        if (had) qputenv("HR_FFMPEG", oldFfmpeg); else qunsetenv("HR_FFMPEG");
    }
    static ai::Transcriber::Request request(const QString& media) {
        ai::Transcriber::Request r;
        r.mediaId = "m1";
        r.mediaPath = media;
        r.duration = 13;
        r.language = "auto";
        r.model.id = "test";
        r.model.file = "ggml-test.bin";
        r.model.minBytes = 1000;
        return r;
    }
};


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


    // ------------------------------------------------------------------ AI helpers: cutting ranges
    void cutRangeKeepsLinkedPairsAndRipples() {
        Timeline tl = Timeline::makeDefault();
        MediaItem m = fakeVideo("a.mp4", 30, true);
        ops::placeClips(tl, ops::makeClips(m, 0, 10, 0), 0, 0, false);
        ops::placeClips(tl, ops::makeClips(m, 10, 5, 10), 0, 0, false);
        const int removed = ops::cutRange(tl, 3.0, 5.0, true);
        QVERIFY(removed >= 2);                                   // picture and sound of the middle piece
        QVERIFY(std::abs(tl.duration() - 13.0) < 1e-6);
        QCOMPARE(tl.video[0].clips.size(), 3);
        QCOMPARE(tl.audio[0].clips.size(), 3);
        // the piece after the cut continues where the removed part ended, and starts right at the cut
        QVERIFY(std::abs(tl.video[0].clips[1].start - 3.0) < 1e-6);
        QVERIFY(std::abs(tl.video[0].clips[1].srcIn - 5.0) < 1e-6);
        QVERIFY(std::abs(tl.video[0].clips[2].start - 8.0) < 1e-6);   // the second clip moved 2 s earlier
        // picture and sound stay paired, but the two halves of the cut clip are no longer linked together
        QCOMPARE(tl.video[0].clips[0].linkId, tl.audio[0].clips[0].linkId);
        QCOMPARE(tl.video[0].clips[1].linkId, tl.audio[0].clips[1].linkId);
        QVERIFY(tl.video[0].clips[0].linkId != tl.video[0].clips[1].linkId);
    }

    void cutRangesWorksBackwardsAndSkipsLockedTracks() {
        Timeline tl = Timeline::makeDefault();
        MediaItem m = fakeVideo("a.mp4", 30, true);
        ops::placeClips(tl, ops::makeClips(m, 0, 10, 0), 0, 0, false);
        ops::cutRanges(tl, {{2.0, 3.0}, {6.0, 8.0}}, true);
        QVERIFY(std::abs(tl.duration() - 7.0) < 1e-6);
        QCOMPARE(tl.video[0].clips.size(), 3);
        QVERIFY(std::abs(tl.video[0].clips[2].srcIn - 8.0) < 1e-6);
        // a locked track is left alone
        Timeline locked = Timeline::makeDefault();
        ops::placeClips(locked, ops::makeClips(m, 0, 10, 0), 0, 0, false);
        locked.audio[0].locked = true;
        ops::cutRange(locked, 2.0, 4.0, true);
        QVERIFY(std::abs(locked.video[0].clips.last().end() - 8.0) < 1e-6);
        QVERIFY(std::abs(locked.audio[0].clips.last().end() - 10.0) < 1e-6);
    }

    // ------------------------------------------------------------------ AI helpers: silence
    static QVector<quint8> burstPeaks(std::initializer_list<std::pair<double, int>> parts) {   // seconds, level 0..255
        QVector<quint8> v;
        for (auto& pr : parts) for (int i = 0; i < int(pr.first * 100); ++i) v.push_back(quint8(pr.second));
        return v;
    }

    void silenceFinderUsesThresholdLengthAndPadding() {
        const QVector<quint8> peaks = burstPeaks({{1, 120}, {2, 1}, {1, 120}, {1.5, 1}, {1, 120}, {0.3, 1}, {1, 120}});
        Timeline tl = Timeline::makeDefault();
        MediaItem m = fakeVideo("talk.mp4", 7.8, true);
        m.id = "talk";
        ops::placeClips(tl, ops::makeClips(m, 0, 7.8, 0), 0, 0, false);
        auto provider = [&](const QString& id) -> const QVector<quint8>* { return id == "talk" ? &peaks : nullptr; };
        ai::SilenceOptions o;                                      // -40 dB, 0.6 s, 0.12 s padding
        const QVector<TimeRange> r = ai::findSilences(tl, provider, o);
        QCOMPARE(r.size(), 2);                                     // the 0.3 s pause is too short to touch
        QVERIFY2(std::abs(r[0].start - 1.12) < 0.04 && std::abs(r[0].end - 2.88) < 0.04, qPrintable(QString("%1-%2").arg(r[0].start).arg(r[0].end)));
        QVERIFY2(std::abs(r[1].start - 4.12) < 0.04 && std::abs(r[1].end - 5.38) < 0.04, qPrintable(QString("%1-%2").arg(r[1].start).arg(r[1].end)));
        // a stricter minimum length finds only the longer one
        o.minSilence = 1.8;
        QCOMPARE(ai::findSilences(tl, provider, o).size(), 1);
        // lowering the clip volume turns quiet speech into "silence"
        const QVector<quint8> quiet = burstPeaks({{3, 40}});       // about -16 dB
        auto quietProvider = [&](const QString&) -> const QVector<quint8>* { return &quiet; };
        Timeline q = Timeline::makeDefault();
        ops::placeClips(q, ops::makeClips(m, 0, 3, 0), 0, 0, false);
        QCOMPARE(ai::findSilences(q, quietProvider, ai::SilenceOptions()).size(), 0);
        q.audio[0].clips[0].effect("volume")->set("level", -30.0);
        QCOMPARE(ai::findSilences(q, quietProvider, ai::SilenceOptions()).size(), 1);
        // unknown waveforms are treated as loud and reported
        bool incomplete = false;
        auto none = [](const QString&) -> const QVector<quint8>* { return nullptr; };
        QCOMPARE(ai::findSilences(tl, none, ai::SilenceOptions(), &incomplete).size(), 0);
        QVERIFY(incomplete);
    }

    void silenceFinderNeverCutsPlacesWithoutSound() {
        const QVector<quint8> peaks = burstPeaks({{1, 120}, {0.2, 1}});
        Timeline tl = Timeline::makeDefault();
        MediaItem m = fakeVideo("talk.mp4", 1.2, true);
        m.id = "talk";
        ops::placeClips(tl, ops::makeClips(m, 0, 1.2, 0), 0, 0, false);
        tl.video[1].clips.push_back(ops::makeTitle(0, 12, "a long title with no sound"));   // 12 s of picture only
        auto provider = [&](const QString&) -> const QVector<quint8>* { return &peaks; };
        QCOMPARE(ai::findSilences(tl, provider, ai::SilenceOptions()).size(), 0);
    }

    void silenceFinderOnRealAudio() {
        // 1 s tone, 2 s silence, 1 s tone, 1.5 s silence, 1 s tone
        const QString wav = dir.filePath("pattern.wav");
        QVERIFY(runFfmpeg({"-f", "lavfi", "-i", "sine=frequency=440:sample_rate=48000:duration=1", "-f", "lavfi", "-i",
                           "anullsrc=r=48000:cl=mono:d=2", "-f", "lavfi", "-i", "sine=frequency=440:sample_rate=48000:duration=1",
                           "-f", "lavfi", "-i", "anullsrc=r=48000:cl=mono:d=1.5", "-f", "lavfi", "-i",
                           "sine=frequency=440:sample_rate=48000:duration=1", "-filter_complex",
                           "[0][1][2][3][4]concat=n=5:v=0:a=1,volume=4", "-ar", "48000", wav}));
        Project p;
        const MediaItem item = *p.mediaById(p.importFile(wav));
        MediaAssets assets;
        assets.request(item);
        QEventLoop loop;
        QTimer guard;
        guard.setSingleShot(true);
        connect(&guard, &QTimer::timeout, &loop, &QEventLoop::quit);
        connect(&assets, &MediaAssets::updated, &loop, [&] { if (assets.get(item.id).wavesReady) loop.quit(); });
        guard.start(10000);
        if (!assets.get(item.id).wavesReady) loop.exec();
        const MediaAsset asset = assets.get(item.id);
        QVERIFY(asset.wavesReady);
        Timeline tl = Timeline::makeDefault();
        ops::placeClips(tl, ops::makeClips(item, 0, item.duration, 0), 0, 0, false);
        auto provider = [&](const QString&) -> const QVector<quint8>* { return &asset.peaks100; };
        const QVector<TimeRange> r = ai::findSilences(tl, provider, ai::SilenceOptions());
        QCOMPARE(r.size(), 2);
        QVERIFY2(std::abs(r[0].start - 1.12) < 0.1 && std::abs(r[0].end - 2.88) < 0.1, qPrintable(QString("%1-%2").arg(r[0].start).arg(r[0].end)));
        QVERIFY2(std::abs(r[1].start - 4.12) < 0.1 && std::abs(r[1].end - 5.38) < 0.1, qPrintable(QString("%1-%2").arg(r[1].start).arg(r[1].end)));
        // cutting them leaves just the three tones plus the padding
        ops::cutRanges(tl, r, true);
        QVERIFY2(std::abs(tl.duration() - (6.5 - 1.76 - 1.26)) < 0.15, qPrintable(QString::number(tl.duration())));
    }

    // ------------------------------------------------------------------ AI helpers: scenes
    void sceneDetectorFindsHardCutsOnly() {
        const QString clip = dir.filePath("three_scenes.mp4");
        QVERIFY(runFfmpeg({"-f", "lavfi", "-i", "testsrc2=size=320x180:rate=30:duration=2", "-f", "lavfi", "-i",
                           "color=c=blue:s=320x180:r=30:d=2", "-f", "lavfi", "-i", "smptebars=size=320x180:rate=30:duration=2",
                           "-filter_complex", "[0][1][2]concat=n=3:v=1:a=0,format=yuv420p", "-c:v", "libx264", clip}));
        const QVector<double> cuts = ai::detectScenes(clip, ai::SceneOptions());
        QCOMPARE(cuts.size(), 2);
        QVERIFY2(std::abs(cuts[0] - 2.0) < 0.07, qPrintable(QString::number(cuts[0])));
        QVERIFY2(std::abs(cuts[1] - 4.0) < 0.07, qPrintable(QString::number(cuts[1])));
        // a single continuous shot (with lots of motion) has no cuts
        const QString single = dir.filePath("one_scene.mp4");
        QVERIFY(runFfmpeg({"-f", "lavfi", "-i", "testsrc2=size=320x180:rate=30:duration=5", "-c:v", "libx264", "-pix_fmt", "yuv420p", single}));
        QCOMPARE(ai::detectScenes(single, ai::SceneOptions()).size(), 0);
    }


    // ------------------------------------------------------------------ AI speech: subtitles and text editing
    void srtRoundTripKeepsArabicAndDropsAnnotations() {
        const QString srt = QString::fromUtf8(
            "1\r\n00:00:01,000 --> 00:00:03,500\r\nHello and welcome.\r\n\r\n"
            "2\r\n00:00:04,000 --> 00:00:05,000\r\n[BLANK_AUDIO]\r\n\r\n"
            "3\r\n00:01:02,250 --> 00:01:05,000\r\nمرحبا بكم\r\nفي القناة\r\n\r\n"
            "4\r\n00:01:06,000 --> 00:01:07,000\r\n(upbeat music)\r\n\r\n");
        const QVector<TranscriptSegment> segs = ai::parseSrt(srt);
        QCOMPARE(segs.size(), 2);
        QVERIFY(std::abs(segs[0].start - 1.0) < 1e-9 && std::abs(segs[0].end - 3.5) < 1e-9);
        QCOMPARE(segs[1].text, QString::fromUtf8("مرحبا بكم في القناة"));   // two lines become one sentence
        QVERIFY(std::abs(segs[1].start - 62.25) < 1e-9);
        const QVector<TranscriptSegment> again = ai::parseSrt(ai::formatSrt(segs));
        QCOMPARE(again.size(), 2);
        QCOMPARE(again[1].text, segs[1].text);
        QVERIFY(std::abs(again[1].end - 65.0) < 1e-3);
        QVERIFY(ai::isNonSpeech("[Music]") && ai::isNonSpeech("♪♪") && ai::isNonSpeech("  ") && !ai::isNonSpeech("Hello"));
    }

    static MediaItem talkingMedia() {
        MediaItem m = fakeVideo("talk.mp4", 30, true);
        m.id = "talk";
        m.transcript = {{1.0, 3.0, "first sentence"}, {5.0, 8.0, "second sentence"}, {12.0, 14.0, "third sentence"}};
        return m;
    }

    void transcriptFollowsTrimmedAndMovedClips() {
        const MediaItem m = talkingMedia();
        Timeline tl = Timeline::makeDefault();
        // the clip shows source seconds 4..13 (second sentence fully, third cut at 13) and starts at 20 s
        ops::placeClips(tl, ops::makeClips(m, 4.0, 9.0, 20.0), 0, 0, false);
        const QVector<ai::TimelineSegment> ts = ai::transcriptOnTimeline(tl, {m});
        QCOMPARE(ts.size(), 2);
        QCOMPARE(ts[0].text, QString("second sentence"));
        QVERIFY(std::abs(ts[0].start - 21.0) < 1e-9 && std::abs(ts[0].end - 24.0) < 1e-9);
        QCOMPARE(ts[1].text, QString("third sentence"));
        QVERIFY(std::abs(ts[1].start - 28.0) < 1e-9 && std::abs(ts[1].end - 29.0) < 1e-9);   // clipped at the clip's end
        // a muted track is ignored
        tl.audio[0].enabled = false;
        QCOMPARE(ai::transcriptOnTimeline(tl, {m}).size(), 0);
    }

    void deletingASentenceCutsPictureAndSound() {
        const MediaItem m = talkingMedia();
        Timeline tl = Timeline::makeDefault();
        ops::placeClips(tl, ops::makeClips(m, 0.0, 20.0, 0.0), 0, 0, false);
        const QVector<ai::TimelineSegment> ts = ai::transcriptOnTimeline(tl, {m});
        QCOMPARE(ts.size(), 3);
        ops::cutRange(tl, ts[1].start, ts[1].end, true);                 // delete the second sentence (5..8 s)
        QVERIFY(std::abs(tl.duration() - 17.0) < 1e-6);
        // the transcript now skips it, and the third sentence moved 3 s earlier
        const QVector<ai::TimelineSegment> after = ai::transcriptOnTimeline(tl, {m});
        QCOMPARE(after.size(), 2);
        QCOMPARE(after[1].text, QString("third sentence"));
        QVERIFY(std::abs(after[1].start - 9.0) < 1e-6);
        QCOMPARE(tl.video[0].clips.size(), 2);
        QCOMPARE(tl.audio[0].clips.size(), 2);
    }

    void keepOnlySelectedSentences() {
        // keep the 1st and 3rd sentence of a 20 s clip, with 0.1 s of handle on each side
        const QVector<TimeRange> cuts = ai::complementRanges({{1.0, 3.0}, {12.0, 14.0}}, 20.0, 0.1);
        QCOMPARE(cuts.size(), 3);
        QVERIFY(std::abs(cuts[0].start - 0.0) < 1e-9 && std::abs(cuts[0].end - 0.9) < 1e-9);
        QVERIFY(std::abs(cuts[1].start - 3.1) < 1e-9 && std::abs(cuts[1].end - 11.9) < 1e-9);
        QVERIFY(std::abs(cuts[2].start - 14.1) < 1e-9 && std::abs(cuts[2].end - 20.0) < 1e-9);
        // overlapping handles merge
        QCOMPARE(ai::mergeRanges({{0, 2}, {1.5, 4}, {6, 7}}).size(), 2);
    }

    void captionsAreSplitAndPlacedOnAFreeTrack() {
        ai::TimelineSegment s;
        s.start = 10;
        s.end = 20;
        s.text = "this is a rather long sentence that must be split into several shorter captions so it fits on the screen";
        const QVector<ai::TimelineSegment> parts = ai::splitForCaptions({s}, 40);
        QVERIFY(parts.size() >= 3);
        QVERIFY(std::abs(parts.first().start - 10) < 1e-9 && std::abs(parts.last().end - 20) < 1e-6);
        for (int i = 0; i < parts.size(); ++i) {
            QVERIFY(parts[i].text.size() <= 40);
            if (i) QVERIFY(std::abs(parts[i].start - parts[i - 1].end) < 1e-9);
        }
        Timeline tl = Timeline::makeDefault();                           // V1..V3 empty
        const int track = ai::addCaptions(tl, parts);
        QCOMPARE(track, 2);                                              // top-most free track
        QCOMPARE(tl.video[2].clips.size(), parts.size());
        QCOMPARE(tl.video[2].clips[0].kind, ClipKind::Title);
        QVERIFY(std::abs(tl.video[2].clips[0].effect("motion")->num("position_y") - 36.0) < 1e-9);
        // every track busy in that range -> a new track is added on top
        for (int i = 0; i < tl.video.size(); ++i)
            if (i != 2) tl.video[i].clips.push_back(ops::makeTitle(0, 30, "busy"));
        const int track2 = ai::addCaptions(tl, parts);
        QCOMPARE(track2, 3);
        QCOMPARE(tl.video.size(), 4);
    }

    void transcriptIsSavedInTheProject() {
        Project p;
        const MediaItem item = *p.mediaById(p.importFile(red));
        p.setTranscript(item.id, {{0.5, 1.5, QString::fromUtf8("مرحبا")}, {2.0, 3.0, "hello"}}, "ar", "small");
        const QString file = dir.filePath("transcript.hrproj");
        QVERIFY(p.save(file));
        Project q;
        QVERIFY(q.load(file));
        const MediaItem* m = q.mediaById(item.id);
        QVERIFY(m);
        QCOMPARE(m->transcript.size(), 2);
        QCOMPARE(m->transcript[0].text, QString::fromUtf8("مرحبا"));
        QCOMPARE(m->transcriptModel, QString("small"));
    }

    // ------------------------------------------------------------------ AI speech: downloading models
    void modelDownloadTriesMirrorsAndValidatesSize() {
        QTemporaryDir aiDir;
        qputenv("HR_AI_DIR", aiDir.path().toUtf8());
        TinyHttpServer server;
        const QString base = QString("http://127.0.0.1:%1").arg(server.port());
        ai::ModelInfo m;
        m.id = "t";
        m.title = "Test model";
        m.file = "ggml-dl.bin";
        m.minBytes = 100000;
        m.urls = {base + "/missing", base + "/small", base + "/ok"};   // 404, then a truncated file, then the real one
        QVERIFY(!ai::AiModels::isInstalled(m));
        ai::AiModels models;
        QSignalSpy done(&models, &ai::AiModels::finished);
        QSignalSpy prog(&models, &ai::AiModels::progress);
        models.download(m);
        QVERIFY(done.wait(15000));
        QCOMPARE(done.at(0).at(0).toBool(), true);
        QVERIFY(!prog.isEmpty());
        QVERIFY(ai::AiModels::isInstalled(m));
        QCOMPARE(QFileInfo(ai::AiModels::pathOf(m)).size(), qint64(300000));
        QVERIFY(!QFileInfo::exists(ai::AiModels::pathOf(m) + ".part"));
        // every mirror broken -> a clear failure and no half file left behind
        ai::ModelInfo bad = m;
        bad.file = "ggml-bad.bin";
        bad.urls = {base + "/missing", base + "/small"};
        QSignalSpy done2(&models, &ai::AiModels::finished);
        models.download(bad);
        QVERIFY(done2.wait(15000));
        QCOMPARE(done2.at(0).at(0).toBool(), false);
        QVERIFY(!ai::AiModels::isInstalled(bad));
        QVERIFY(!QFileInfo::exists(ai::AiModels::pathOf(bad) + ".part"));
        qunsetenv("HR_AI_DIR");
    }

    void modelListIsSane() {
        const QVector<ai::ModelInfo> list = ai::AiModels::models();
        QVERIFY(list.size() >= 4);
        for (const ai::ModelInfo& m : list) {
            QVERIFY(!m.urls.isEmpty() && m.urls[0].startsWith("https://"));
            QVERIFY(m.file.endsWith(".bin"));
            QVERIFY(m.minBytes > 0);
        }
        QVERIFY(ai::AiModels::modelById("small").valid());
        QVERIFY(!ai::AiModels::modelById("nope").valid());
    }

    // ------------------------------------------------------------------ AI speech: running the recogniser
    void transcriberParsesTheRecogniserOutput() {
        FakeFfmpegEnv env;
        QVERIFY(QFileInfo::exists(Exporter::findFfmpeg()));
        QVERIFY(ai::Transcriber::supportsWhisper(Exporter::findFfmpeg()));
        ai::Transcriber tr;
        QSignalSpy done(&tr, &ai::Transcriber::finished);
        QSignalSpy prog(&tr, &ai::Transcriber::progress);
        tr.start(FakeFfmpegEnv::request(red));
        QVERIFY(done.wait(15000));
        QCOMPARE(done.at(0).at(0).toBool(), true);
        const auto segs = done.at(0).at(1).value<QVector<TranscriptSegment>>();
        QCOMPARE(segs.size(), 3);                                       // "[BLANK_AUDIO]" is dropped
        QCOMPARE(segs[0].text, QString("Hello and welcome to the channel."));
        QVERIFY(std::abs(segs[0].start - 0.2) < 1e-9 && std::abs(segs[0].end - 1.2) < 1e-9);
        QCOMPARE(segs[2].text, QString::fromUtf8("مرحبا بكم في القناة"));
        QVERIFY(!prog.isEmpty());                                       // out_time_us lines became progress
        QVERIFY(!QFileInfo::exists(env.aiDir.filePath("transcript_m1.srt")));   // temp file cleaned up
    }

    void transcriberRetriesWithoutVadAndReportsFailures() {
        FakeFfmpegEnv env;
        {
            qputenv("FAKE_FFMPEG_FAIL_VAD", "1");                       // the VAD model "does not work" -> second try without it
            ai::Transcriber tr;
            QSignalSpy done(&tr, &ai::Transcriber::finished);
            tr.start(FakeFfmpegEnv::request(red));
            QVERIFY(done.wait(15000));
            QCOMPARE(done.at(0).at(0).toBool(), true);
            QCOMPARE(done.at(0).at(1).value<QVector<TranscriptSegment>>().size(), 3);
        }
        {
            qputenv("FAKE_FFMPEG_FAIL", "1");
            ai::Transcriber tr;
            QSignalSpy done(&tr, &ai::Transcriber::finished);
            tr.start(FakeFfmpegEnv::request(red));
            QVERIFY(done.wait(15000));
            QCOMPARE(done.at(0).at(0).toBool(), false);
            QVERIFY(done.at(0).at(2).toString().contains("simulated failure"));
        }
        {
            qunsetenv("FAKE_FFMPEG_FAIL");
            ai::Transcriber tr;
            QSignalSpy done(&tr, &ai::Transcriber::finished);
            ai::Transcriber::Request r = FakeFfmpegEnv::request(red);
            r.model.file = "ggml-missing.bin";                          // not installed
            tr.start(r);
            QVERIFY(done.count() == 1 && !done.at(0).at(0).toBool());
        }
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
