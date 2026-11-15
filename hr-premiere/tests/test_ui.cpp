// UI tests: drive the real main window (offscreen) with synthetic mouse / keyboard events.
#include "core/EditOps.h"
#include "engine/Exporter.h"
#include "ui/AiPanel.h"
#include "ui/Dialogs.h"
#include "ui/MainWindow.h"
#include "ui/ParticleField.h"
#include "engine/VoiceRecorder.h"
#include "ui/MonitorPanel.h"
#include "ui/ProjectPanel.h"
#include "ui/Theme.h"

#include <QApplication>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QMimeData>
#include <QProcess>
#include <QTemporaryDir>
#include <QtTest>

using namespace hr;

static bool makeVideo(const QString& path, const QString& color, double dur, bool audio) {
    QStringList args = {"-hide_banner", "-loglevel", "error", "-y", "-f", "lavfi", "-i",
                        QString("color=c=%1:s=320x180:r=30:d=%2").arg(color).arg(dur)};
    if (audio) args << "-f" << "lavfi" << "-i" << QString("sine=frequency=440:duration=%1").arg(dur);
    args << "-c:v" << "libx264" << "-pix_fmt" << "yuv420p";
    if (audio) args << "-c:a" << "aac" << "-shortest";
    args << path;
    return QProcess::execute(Exporter::findFfmpeg().isEmpty() ? "ffmpeg" : Exporter::findFfmpeg(), args) == 0;
}

// Points HR_FFMPEG at the fake ffmpeg (it "recognises speech" instantly) and fakes an installed speech model
// (a sparse file of the right size) so the AI panel can be driven without downloads.
struct FakeSpeechEnv {
    QTemporaryDir aiDir;
    bool had;
    QByteArray old;
    FakeSpeechEnv() {
        had = qEnvironmentVariableIsSet("HR_FFMPEG");
        old = qgetenv("HR_FFMPEG");
#ifdef Q_OS_WIN
        const QString fake = QCoreApplication::applicationDirPath() + "/fake_ffmpeg.exe";
#else
        const QString fake = QCoreApplication::applicationDirPath() + "/fake_ffmpeg";
#endif
        qputenv("HR_FFMPEG", fake.toUtf8());
        qputenv("HR_AI_DIR", aiDir.path().toUtf8());
        QFile m(aiDir.filePath("ggml-small.bin"));
        m.open(QIODevice::WriteOnly);
        m.resize(450ll << 20);
        QFile v(aiDir.filePath("ggml-silero-v5.1.2.bin"));
        v.open(QIODevice::WriteOnly);
        v.write(QByteArray(500 * 1024, 'v'));
    }
    ~FakeSpeechEnv() {
        qunsetenv("HR_AI_DIR");
        if (had) qputenv("HR_FFMPEG", old); else qunsetenv("HR_FFMPEG");
    }
};

class UiTests : public QObject {
    Q_OBJECT
    QTemporaryDir dir;
    QString red, blue, green;
    MainWindow* w = nullptr;

    Project* proj() { return w->project(); }
    TimelineWidget* tl() { return w->timeline(); }
    QPoint at(const QRectF& r, double fx, double fy) { return QPoint(int(r.left() + r.width() * fx), int(r.top() + r.height() * fy)); }
    void settle() { QCoreApplication::processEvents(); }
    void resetTimeline() {
        proj()->clear();
        proj()->undoStack()->clear();
        settle();
    }
    void load() {
        resetTimeline();
        w->autoPlace({red, blue});  // red 0..4 (with audio), blue 4..6 (video only)
        settle();
    }

private slots:
    void initTestCase() {
        QVERIFY(dir.isValid());
        red = dir.filePath("red.mp4");
        blue = dir.filePath("blue.mp4");
        green = dir.filePath("green.mp4");
        QVERIFY(makeVideo(red, "red", 4, true));
        QVERIFY(makeVideo(blue, "blue", 2, false));
        QVERIFY(makeVideo(green, "green", 3, true));
        QStandardPaths::setTestModeEnabled(true);   // keep recordings / settings out of the real profile
        theme::apply(*qApp);
        w = new MainWindow(false);
        w->resize(1500, 900);
        w->show();
        QVERIFY(QTest::qWaitForWindowExposed(w));
    }
    void cleanupTestCase() {
        delete w;
        w = nullptr;
    }

    void importAndAutoPlace() {
        load();
        QCOMPARE(proj()->media().size(), 2);
        QCOMPARE(proj()->timeline().video[0].clips.size(), 2);
        QCOMPARE(proj()->timeline().audio[0].clips.size(), 1);
        QCOMPARE(proj()->timeline().width, 320);   // first clip set the sequence settings
        QCOMPARE(proj()->timeline().height, 180);
        QVERIFY(std::abs(proj()->timeline().duration() - 6.0) < 0.1);
    }

    void clickSelectsLinkedPair() {
        load();
        const Clip v = proj()->timeline().video[0].clips[0];
        const QRectF r = tl()->clipRectForId(v.id);
        QTest::mouseClick(tl(), Qt::LeftButton, {}, at(r, 0.5, 0.6));
        QCOMPARE(proj()->selection().size(), 2);  // video + its linked audio
        QVERIFY(proj()->selection().contains(proj()->timeline().audio[0].clips[0].id));
        // click on empty space deselects
        QTest::mouseClick(tl(), Qt::LeftButton, {}, QPoint(int(r.right() + 300), int(r.center().y())));
        QCOMPARE(proj()->selection().size(), 0);
    }

    void dragMovesClipAndUndoRestores() {
        load();
        const Clip blue0 = proj()->timeline().video[0].clips[1];
        const QRectF r = tl()->clipRectForId(blue0.id);
        const QPoint p0 = at(r, 0.5, 0.6);
        // drag the blue clip 3 s to the right (80 px / s at the default zoom)
        const double pps = tl()->pixelsPerSecond();
        QTest::mousePress(tl(), Qt::LeftButton, {}, p0);
        QTest::mouseMove(tl(), p0 + QPoint(int(pps), 0));
        QTest::mouseMove(tl(), p0 + QPoint(int(3 * pps), 0));
        QTest::mouseRelease(tl(), Qt::LeftButton, {}, p0 + QPoint(int(3 * pps), 0));
        const Clip moved = *proj()->timeline().clip(blue0.id);
        QVERIFY2(std::abs(moved.start - (blue0.start + 3.0)) < 0.2, qPrintable(QString::number(moved.start)));
        proj()->undoStack()->undo();
        QVERIFY(std::abs(proj()->timeline().clip(blue0.id)->start - blue0.start) < 1e-6);
        proj()->undoStack()->redo();
        QVERIFY(std::abs(proj()->timeline().clip(blue0.id)->start - (blue0.start + 3.0)) < 0.2);
    }

    void dragToAnotherTrack() {
        load();
        const Clip blue0 = proj()->timeline().video[0].clips[1];
        const QRectF r = tl()->clipRectForId(blue0.id);
        const QPoint p0 = at(r, 0.5, 0.6);
        QTest::mousePress(tl(), Qt::LeftButton, {}, p0);
        QTest::mouseMove(tl(), p0 + QPoint(0, -30));
        QTest::mouseMove(tl(), p0 + QPoint(0, -62));   // one video track up
        QTest::mouseRelease(tl(), Qt::LeftButton, {}, p0 + QPoint(0, -62));
        QCOMPARE(proj()->timeline().video[1].clips.size(), 1);
        QCOMPARE(proj()->timeline().video[0].clips.size(), 1);
    }

    void trimRightEdge() {
        load();
        const Clip red0 = proj()->timeline().video[0].clips[0];
        const QRectF r = tl()->clipRectForId(red0.id);
        const QPoint edge(int(r.right()) - 2, int(r.center().y()) + 4);
        const double pps = tl()->pixelsPerSecond();
        QTest::mousePress(tl(), Qt::LeftButton, {}, edge);
        QTest::mouseMove(tl(), edge - QPoint(int(pps), 0));
        QTest::mouseMove(tl(), edge - QPoint(int(2 * pps), 0));
        QTest::mouseRelease(tl(), Qt::LeftButton, {}, edge - QPoint(int(2 * pps), 0));
        const Clip now = *proj()->timeline().clip(red0.id);
        QVERIFY2(std::abs(now.duration - 2.0) < 0.25, qPrintable(QString::number(now.duration)));
        // the linked audio clip was trimmed too
        QVERIFY(std::abs(proj()->timeline().audio[0].clips[0].duration - now.duration) < 1e-6);
    }

    void razorSplitsClipAndItsAudio() {
        load();
        tl()->setTool(Tool::Razor);
        const Clip red0 = proj()->timeline().video[0].clips[0];
        const QRectF r = tl()->clipRectForId(red0.id);
        QTest::mouseClick(tl(), Qt::LeftButton, {}, at(r, 0.5, 0.6));
        QCOMPARE(proj()->timeline().video[0].clips.size(), 3);
        QCOMPARE(proj()->timeline().audio[0].clips.size(), 2);
        tl()->setTool(Tool::Selection);
    }

    void deleteAndRippleDelete() {
        load();
        const Clip red0 = proj()->timeline().video[0].clips[0];
        const QRectF r = tl()->clipRectForId(red0.id);
        QTest::mouseClick(tl(), Qt::LeftButton, {}, at(r, 0.5, 0.6));
        QTest::keyClick(w, Qt::Key_Delete, Qt::ShiftModifier);   // ripple delete
        QCOMPARE(proj()->timeline().video[0].clips.size(), 1);
        QVERIFY(std::abs(proj()->timeline().video[0].clips[0].start) < 1e-6);   // blue moved to 0
        QCOMPARE(proj()->timeline().audio[0].clips.size(), 0);
        QTest::keyClick(w, Qt::Key_Z, Qt::ControlModifier);
        QCOMPARE(proj()->timeline().video[0].clips.size(), 2);
        QCOMPARE(proj()->timeline().audio[0].clips.size(), 1);
    }

    void splitAtPlayheadShortcut() {
        load();
        w->program()->seek(2.0);
        settle();
        QTest::keyClick(w, Qt::Key_K, Qt::ControlModifier);
        QCOMPARE(proj()->timeline().video[0].clips.size(), 3);
        QCOMPARE(proj()->timeline().audio[0].clips.size(), 2);
    }

    void spaceTogglesPlayback() {
        load();
        w->program()->seek(0.5);
        QTest::keyClick(w, Qt::Key_Space);
        QVERIFY(w->program()->isPlaying());
        QTest::qWait(700);
        QVERIFY2(w->program()->position() > 0.9, qPrintable(QString::number(w->program()->position())));
        QTest::keyClick(w, Qt::Key_Space);
        QVERIFY(!w->program()->isPlaying());
        const double stopped = w->program()->position();
        QTest::qWait(200);
        QCOMPARE(w->program()->position(), stopped);
        // playback runs to the end and stops by itself
        w->program()->seek(5.6);
        w->program()->play();
        QTRY_VERIFY_WITH_TIMEOUT(!w->program()->isPlaying(), 3000);
        QVERIFY(w->program()->position() >= 5.9);
    }

    void arrowKeysStepFrames() {
        load();
        w->program()->seek(1.0);
        QTest::keyClick(w, Qt::Key_Right);
        QVERIFY(std::abs(w->program()->position() - (1.0 + 1.0 / 30)) < 1e-3);
        QTest::keyClick(w, Qt::Key_Left, Qt::ShiftModifier);
        QVERIFY(std::abs(w->program()->position() - (1.0 + 1.0 / 30 - 5.0 / 30)) < 1e-3);
        QTest::keyClick(w, Qt::Key_Down);   // next edit point
        QVERIFY(std::abs(w->program()->position() - 4.0) < 1e-3);
    }

    void markersAndInOut() {
        load();
        w->program()->seek(1.0);
        QTest::keyClick(w, Qt::Key_I);
        w->program()->seek(3.0);
        QTest::keyClick(w, Qt::Key_O);
        QVERIFY(std::abs(proj()->timeline().markIn - 1.0) < 1e-3);
        QVERIFY(std::abs(proj()->timeline().markOut - 3.0) < 1e-3);
        QTest::keyClick(w, Qt::Key_M);
        QCOMPARE(proj()->timeline().markers.size(), 1);
        QTest::keyClick(w, Qt::Key_X, Qt::ControlModifier | Qt::ShiftModifier);
        QVERIFY(proj()->timeline().markIn < 0 && proj()->timeline().markOut < 0);
    }

    void dropFromProjectPanel() {
        resetTimeline();
        const QString id = proj()->importFile(green);
        QVERIFY(!id.isEmpty());
        settle();
        // find the V1 row: any point inside the video area at the timeline's V1 line
        const double pps = tl()->pixelsPerSecond();
        auto* mime = new QMimeData;
        mime->setData("application/x-hr-media", id.toUtf8());
        // V1 is the lowest video row: use the row geometry via a probe clip
        proj()->edit("probe", [&](Timeline& t) { ops::placeClips(t, ops::makeClips(*proj()->mediaById(id), 0, 1, 20), 0, 0, false); });
        const QRectF probe = tl()->clipRectForId(proj()->timeline().video[0].clips[0].id);
        proj()->undoStack()->undo();
        settle();
        const QPoint pos(int(150 + 2 * pps), int(probe.center().y()));
        QDragEnterEvent enter(pos, Qt::CopyAction, mime, Qt::LeftButton, {});
        QApplication::sendEvent(tl(), &enter);
        QDragMoveEvent move(pos, Qt::CopyAction, mime, Qt::LeftButton, {});
        QApplication::sendEvent(tl(), &move);
        QDropEvent drop(QPointF(pos), Qt::CopyAction, mime, Qt::LeftButton, {});
        QApplication::sendEvent(tl(), &drop);
        QCOMPARE(proj()->timeline().video[0].clips.size(), 1);
        QCOMPARE(proj()->timeline().audio[0].clips.size(), 1);
        QVERIFY(proj()->timeline().video[0].clips[0].start > 1.0);
        delete mime;
    }

    void effectsAndTitles() {
        load();
        const Clip red0 = proj()->timeline().video[0].clips[0];
        proj()->setSelection({red0.id});
        // apply an effect from the browser's signal path
        QMetaObject::invokeMethod(w, [&] {}, Qt::DirectConnection);
        auto* browser = w->findChild<EffectsBrowser*>();
        QVERIFY(browser);
        emit browser->applyEffect("gaussian_blur");
        QVERIFY(proj()->timeline().clip(red0.id)->effect("gaussian_blur") != nullptr);
        // the Effect Controls panel shows it and edits go through undo
        auto* panel = w->findChild<EffectControlsPanel*>();
        QVERIFY(panel);
        const auto spins = panel->findChildren<QDoubleSpinBox*>();
        QVERIFY(spins.size() >= 8);
        // title via the Type tool
        tl()->setTool(Tool::Type);
        const QRectF r = tl()->clipRectForId(red0.id);
        QTest::mouseClick(tl(), Qt::LeftButton, {}, QPoint(int(r.left() + 30), int(r.top() - 58)));   // V2 row, 0.4 s in
        int titles = 0;
        for (const Track& t : proj()->timeline().video)
            for (const Clip& c : t.clips) titles += c.kind == ClipKind::Title;
        QCOMPARE(titles, 1);
        tl()->setTool(Tool::Selection);
    }

    void saveAndReopenProject() {
        load();
        const QString file = dir.filePath("ui.hrproj");
        QVERIFY(proj()->save(file));
        QVERIFY(w->openProjectFile(file));
        QCOMPARE(proj()->timeline().video[0].clips.size(), 2);
        QCOMPARE(proj()->media().size(), 2);
        QVERIFY(!proj()->isDirty());
    }

    void copyPaste() {
        load();
        const Clip blue0 = proj()->timeline().video[0].clips[1];
        proj()->setSelection({blue0.id});
        QTest::keyClick(w, Qt::Key_C, Qt::ControlModifier);
        w->program()->seek(10.0);
        QTest::keyClick(w, Qt::Key_V, Qt::ControlModifier);
        QCOMPARE(proj()->timeline().video[0].clips.size(), 3);
        QVERIFY(std::abs(proj()->timeline().video[0].clips[2].start - 10.0) < 0.05);
    }


    void sourceMonitorMarksAndInsert() {
        resetTimeline();
        const QString id = proj()->importFile(green);   // 3 s with audio
        QVERIFY(!id.isEmpty());
        settle();
        auto* panel = w->findChild<ProjectPanel*>();
        QVERIFY(panel);
        emit panel->openInSource(id);                   // what a double-click in the Project panel does
        settle();
        MonitorPanel* source = nullptr;
        for (MonitorPanel* m : w->findChildren<MonitorPanel*>())
            if (m->player() != w->program()) source = m;
        QVERIFY(source);
        QVERIFY(std::abs(source->player()->duration() - 3.0) < 0.15);
        // mark 1 s .. 2.5 s through the monitor's signals
        source->player()->seek(1.0);
        emit source->markInRequested(1.0);
        emit source->markOutRequested(2.5);
        QVERIFY(std::abs(proj()->mediaById(id)->markIn - 1.0) < 1e-6);
        QVERIFY(std::abs(proj()->mediaById(id)->markOut - 2.5) < 1e-6);
        // overwrite at the playhead (4 s)
        w->program()->seek(4.0);
        emit source->overwriteRequested();
        QCOMPARE(proj()->timeline().video[0].clips.size(), 1);
        const Clip c = proj()->timeline().video[0].clips[0];
        QVERIFY(std::abs(c.start - 4.0) < 0.05);
        QVERIFY(std::abs(c.srcIn - 1.0) < 1e-6);
        QVERIFY(std::abs(c.duration - 1.5) < 0.05);
        QCOMPARE(proj()->timeline().audio[0].clips.size(), 1);
        // insert at 4.5 s opens a gap and pushes the rest of the clip later
        w->program()->seek(4.5);
        emit source->insertRequested();
        QCOMPARE(proj()->timeline().video[0].clips.size(), 3);
        QVERIFY(std::abs(proj()->timeline().duration() - 7.0) < 0.1);
    }

    void effectDraggedOntoClip() {
        load();
        const Clip red0 = proj()->timeline().video[0].clips[0];
        const QRectF r = tl()->clipRectForId(red0.id);
        auto* mime = new QMimeData;
        mime->setData("application/x-hr-effect", "invert");
        const QPoint pos = at(r, 0.5, 0.6);
        QDragEnterEvent enter(pos, Qt::CopyAction, mime, Qt::LeftButton, {});
        QApplication::sendEvent(tl(), &enter);
        QDropEvent drop(QPointF(pos), Qt::CopyAction, mime, Qt::LeftButton, {});
        QApplication::sendEvent(tl(), &drop);
        QVERIFY(proj()->timeline().clip(red0.id)->effect("invert") != nullptr);
        QVERIFY(proj()->timeline().clip(red0.id)->hasCustomEffects());
        // an audio effect is refused on a picture clip
        auto* mime2 = new QMimeData;
        mime2->setData("application/x-hr-effect", "balance");
        QDropEvent drop2(QPointF(pos), Qt::CopyAction, mime2, Qt::LeftButton, {});
        QApplication::sendEvent(tl(), &drop2);
        QVERIFY(proj()->timeline().clip(red0.id)->effect("balance") == nullptr);
        delete mime;
        delete mime2;
    }

    void trackToggles() {
        load();
        // lock V1 through the header buttons: the click position is inside the lock icon of the V1 row
        const Clip v = proj()->timeline().video[0].clips[0];
        const QRectF r = tl()->clipRectForId(v.id);
        QTest::mouseClick(tl(), Qt::LeftButton, {}, QPoint(150 - 20, int(r.center().y())));   // lock icon
        QVERIFY(proj()->timeline().video[0].locked);
        // moving a clip on a locked track does nothing
        const double before = proj()->timeline().video[0].clips[1].start;
        const QRectF rb = tl()->clipRectForId(proj()->timeline().video[0].clips[1].id);
        const QPoint p0 = at(rb, 0.5, 0.6);
        QTest::mousePress(tl(), Qt::LeftButton, {}, p0);
        QTest::mouseMove(tl(), p0 + QPoint(120, 0));
        QTest::mouseRelease(tl(), Qt::LeftButton, {}, p0 + QPoint(120, 0));
        QVERIFY(std::abs(proj()->timeline().video[0].clips[1].start - before) < 1e-6);
        QTest::mouseClick(tl(), Qt::LeftButton, {}, QPoint(150 - 20, int(r.center().y())));   // unlock
        QVERIFY(!proj()->timeline().video[0].locked);
    }


    void altClickDeletesOnlyTheSound() {
        load();   // red.mp4 = picture on V1 + sound on A1 (linked), blue.mp4 = picture only
        const Clip audio = proj()->timeline().audio[0].clips[0];
        const QRectF r = tl()->clipRectForId(audio.id);
        QTest::mouseClick(tl(), Qt::LeftButton, Qt::AltModifier, at(r, 0.5, 0.6));   // Alt = ignore the link
        QCOMPARE(proj()->selection().size(), 1);
        QTest::keyClick(w, Qt::Key_Delete);
        QCOMPARE(proj()->timeline().audio[0].clips.size(), 0);   // the sound is gone ...
        QCOMPARE(proj()->timeline().video[0].clips.size(), 2);   // ... the pictures stay
        // a plain click still selects (and therefore deletes) the linked pair
        QTest::keyClick(w, Qt::Key_Z, Qt::ControlModifier);
        const Clip video = proj()->timeline().video[0].clips[0];
        QTest::mouseClick(tl(), Qt::LeftButton, {}, at(tl()->clipRectForId(video.id), 0.5, 0.6));
        QCOMPARE(proj()->selection().size(), 2);
        QTest::keyClick(w, Qt::Key_Delete);
        QCOMPARE(proj()->timeline().audio[0].clips.size(), 0);
        QCOMPARE(proj()->timeline().video[0].clips.size(), 1);
    }

    void muteButtonSilencesTheTrack() {
        load();
        const Clip audio = proj()->timeline().audio[0].clips[0];
        const int y = int(tl()->clipRectForId(audio.id).center().y());
        QTest::mouseClick(tl(), Qt::LeftButton, {}, QPoint(150 - 80, y));   // the "M" button in the A1 header
        QVERIFY(!proj()->timeline().audio[0].enabled);
        QTest::mouseClick(tl(), Qt::LeftButton, {}, QPoint(150 - 80, y));
        QVERIFY(proj()->timeline().audio[0].enabled);
        QTest::mouseClick(tl(), Qt::LeftButton, {}, QPoint(150 - 56, y));   // "S" = solo
        QVERIFY(proj()->timeline().audio[0].solo);
        QTest::mouseClick(tl(), Qt::LeftButton, {}, QPoint(150 - 56, y));
        QVERIFY(!proj()->timeline().audio[0].solo);
    }


    void voiceOverRecordsAndPlacesClip() {
        if (!VoiceRecorder::hasMicrophone()) QSKIP("no microphone in this environment");
        load();                                      // V1 + A1 (sound of red.mp4), 6 s long
        const int audioRowY = int(tl()->clipRectForId(proj()->timeline().audio[0].clips[0].id).center().y());
        w->program()->seek(1.0);
        const int mediaBefore = proj()->media().size();
        QTest::mouseClick(tl(), Qt::LeftButton, {}, QPoint(48, audioRowY + 48));      // the microphone button of A2
        // 3-2-1 countdown, then recording starts and the picture plays along
        QTRY_VERIFY_WITH_TIMEOUT(w->program()->isPlaying(), 6000);
        QTest::qWait(1500);
        QTest::keyClick(w, Qt::Key_Space);           // Space stops the recording
        QTRY_COMPARE_WITH_TIMEOUT(proj()->media().size(), mediaBefore + 1, 3000);
        QCOMPARE(proj()->timeline().audio[1].clips.size(), 1);
        const Clip vo = proj()->timeline().audio[1].clips[0];
        QVERIFY(vo.name.startsWith("Voice-over"));
        QVERIFY(std::abs(vo.start - 1.0) < 0.05);
        QVERIFY2(vo.duration > 1.0, qPrintable(QString::number(vo.duration)));
        QVERIFY(!w->program()->isPlaying());
        proj()->undoStack()->undo();                 // placing it is one undo step
        QCOMPARE(proj()->timeline().audio[1].clips.size(), 0);
    }


    void ownershipPageHasLogosAndAnimatedBackground() {
        OwnershipDialog dlg;
        dlg.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dlg));
        QStringList texts;
        for (QLabel* l : dlg.findChildren<QLabel*>()) texts << l->text();
        QVERIFY(texts.contains("الملكية"));
        QVERIFY(texts.contains("HR"));
        QVERIFY(texts.contains("المالك والمطور الأساسي"));   // HR is the owner and the main developer
        QVERIFY(!texts.contains("المالك"));                    // Kaylar: only logo and name, no caption
        QVERIFY(!texts.contains("Owner"));
        // the background animates
        const QImage a = dlg.grab().toImage();
        QTest::qWait(400);
        const QImage b = dlg.grab().toImage();
        QVERIFY(a != b);
    }

    void particleFieldStaysInsideTheWindow() {
        ParticleField f(150);
        f.resize(QSizeF(800, 450));
        QCOMPARE(f.count(), 150);
        for (int i = 0; i < 3000; ++i) f.update(16);
        for (const ParticleField::Particle& p : f.particles()) {
            QVERIFY(p.pos.x() >= 0 && p.pos.x() <= 800);
            QVERIFY(p.pos.y() >= 0 && p.pos.y() <= 450);
        }
        f.resize(QSizeF(400, 225));                  // shrinking keeps the dots inside
        for (const ParticleField::Particle& p : f.particles()) QVERIFY(p.pos.x() <= 400 && p.pos.y() <= 225);
    }


    // ------------------------------------------------------------------ AI Assistant
    void aiRemovesSilencesFromRealAudio() {
        resetTimeline();
        const QString wav = dir.filePath("pattern.wav");   // 1 s tone, 2 s silence, 1 s tone, 1.5 s silence, 1 s tone
        QVERIFY(QProcess::execute(Exporter::findFfmpeg().isEmpty() ? "ffmpeg" : Exporter::findFfmpeg(),
                                  {"-hide_banner", "-loglevel", "error", "-y", "-f", "lavfi", "-i", "sine=frequency=440:sample_rate=48000:duration=1",
                                   "-f", "lavfi", "-i", "anullsrc=r=48000:cl=mono:d=2", "-f", "lavfi", "-i",
                                   "sine=frequency=440:sample_rate=48000:duration=1", "-f", "lavfi", "-i", "anullsrc=r=48000:cl=mono:d=1.5",
                                   "-f", "lavfi", "-i", "sine=frequency=440:sample_rate=48000:duration=1", "-filter_complex",
                                   "[0][1][2][3][4]concat=n=5:v=0:a=1,volume=4", "-ar", "48000", wav}) == 0);
        w->autoPlace({wav});
        settle();
        QCOMPARE(proj()->timeline().audio[0].clips.size(), 1);
        auto* panel = w->findChild<AiPanel*>();
        QVERIFY(panel);
        const double before = proj()->timeline().duration();
        QVERIFY(std::abs(before - 6.5) < 0.1);
        // the waveform is analysed in the background: ask until it is ready
        QTRY_VERIFY_WITH_TIMEOUT(([&] { panel->removeSilences(); return proj()->timeline().duration() < before - 1.0; }()), 10000);
        QVERIFY2(std::abs(proj()->timeline().duration() - 3.48) < 0.2, qPrintable(QString::number(proj()->timeline().duration())));
        QCOMPARE(proj()->timeline().audio[0].clips.size(), 3);       // the three tones remain
        proj()->undoStack()->undo();                                  // one undo step brings everything back
        QVERIFY(std::abs(proj()->timeline().duration() - before) < 1e-6);
    }

    void aiEditsByTextAndAddsCaptions() {
        load();                                                       // red.mp4 (4 s, sound) + blue.mp4 (2 s)
        auto* panel = w->findChild<AiPanel*>();
        QVERIFY(panel);
        const QString mediaId = proj()->timeline().audio[0].clips[0].mediaId;
        proj()->setTranscript(mediaId, {{0.5, 1.5, "first sentence"}, {2.0, 3.0, "second sentence"}, {3.2, 3.9, "third sentence"}}, "en", "small");
        QTRY_COMPARE(panel->sentenceCount(), 3);
        // searching selects every sentence with the word, deleting cuts picture and sound together
        panel->selectMatching("second");
        const double before = proj()->timeline().duration();
        panel->deleteSentences();
        QVERIFY(std::abs(proj()->timeline().duration() - (before - 1.0)) < 0.05);
        QCOMPARE(panel->sentenceCount(), 2);
        QCOMPARE(proj()->timeline().video[0].clips.size(), 3);       // red.mp4 was cut in two, blue.mp4
        QCOMPARE(proj()->timeline().audio[0].clips.size(), 2);
        // captions for everything
        panel->selectSentences({});
        const int titlesBefore = [&] { int n = 0; for (const Track& t : proj()->timeline().video) for (const Clip& c : t.clips) n += c.kind == ClipKind::Title; return n; }();
        panel->addCaptions(false);
        int titles = 0;
        QString firstText;
        for (const Track& t : proj()->timeline().video)
            for (const Clip& c : t.clips)
                if (c.kind == ClipKind::Title) { ++titles; if (firstText.isEmpty()) firstText = c.effect("text")->text("text"); }
        QCOMPARE(titles - titlesBefore, 2);
        QVERIFY(firstText.contains("sentence"));
        // keep only the first sentence: everything else is removed
        panel->selectSentences({0});
        panel->keepSelectedSentences();
        QVERIFY2(proj()->timeline().duration() < 2.0, qPrintable(QString::number(proj()->timeline().duration())));
    }

    void aiTranscribesAndAutoEditsWithTheFakeRecogniser() {
        FakeSpeechEnv env;
        load();
        auto* panel = w->findChild<AiPanel*>();
        QVERIFY(panel);
        const QString mediaId = proj()->timeline().audio[0].clips[0].mediaId;
        QVERIFY(proj()->mediaById(mediaId)->transcript.isEmpty());
        proj()->setSelection({});                                      // nothing selected -> everything without a transcript
        panel->transcribeSelection();
        QTRY_VERIFY_WITH_TIMEOUT(!panel->isBusy(), 20000);
        QCOMPARE(proj()->mediaById(mediaId)->transcript.size(), 3);   // "[BLANK_AUDIO]" was dropped
        QVERIFY(!proj()->mediaById(mediaId)->transcript[2].text.isEmpty());
        QCOMPARE(panel->sentenceCount(), 3);
        // transcripts are not repeated for media that already have one
        // Auto Edit: no new transcription needed, silences + captions in one undo step
        const int undoBefore = proj()->undoStack()->index();
        panel->autoEdit(false, true);
        QVERIFY(proj()->undoStack()->index() > undoBefore);
        int titles = 0;
        for (const Track& t : proj()->timeline().video) for (const Clip& c : t.clips) titles += c.kind == ClipKind::Title;
        QVERIFY(titles >= 3);
        proj()->undoStack()->undo();                                   // the whole Auto Edit is one step
        int titlesAfter = 0;
        for (const Track& t : proj()->timeline().video) for (const Clip& c : t.clips) titlesAfter += c.kind == ClipKind::Title;
        QCOMPARE(titlesAfter, 0);
    }

    void aiAutoEditTranscribesFirstWhenNeeded() {
        FakeSpeechEnv env;
        load();
        auto* panel = w->findChild<AiPanel*>();
        const QString mediaId = proj()->timeline().audio[0].clips[0].mediaId;
        panel->autoEdit(false, true);                                  // no transcript yet: listen first, then caption
        QTRY_VERIFY_WITH_TIMEOUT(!panel->isBusy() && !proj()->mediaById(mediaId)->transcript.isEmpty(), 20000);
        QTRY_VERIFY_WITH_TIMEOUT(([&] { int n = 0; for (const Track& t : proj()->timeline().video) for (const Clip& c : t.clips) n += c.kind == ClipKind::Title; return n >= 3; }()), 5000);
    }

    void aiDetectsAndCutsScenes() {
        resetTimeline();
        const QString clip = dir.filePath("three_scenes_ui.mp4");
        QVERIFY(QProcess::execute(Exporter::findFfmpeg().isEmpty() ? "ffmpeg" : Exporter::findFfmpeg(),
                                  {"-hide_banner", "-loglevel", "error", "-y", "-f", "lavfi", "-i", "color=c=red:s=320x180:r=30:d=2", "-f", "lavfi",
                                   "-i", "color=c=blue:s=320x180:r=30:d=2", "-f", "lavfi", "-i", "color=c=green:s=320x180:r=30:d=2",
                                   "-filter_complex", "[0][1][2]concat=n=3:v=1:a=0,format=yuv420p", "-c:v", "libx264", clip}) == 0);
        w->autoPlace({clip});
        settle();
        QCOMPARE(proj()->timeline().video[0].clips.size(), 1);
        auto* panel = w->findChild<AiPanel*>();
        QVERIFY(panel);
        panel->detectScenes(false);                                    // markers first
        QTRY_VERIFY_WITH_TIMEOUT(!panel->isBusy(), 20000);
        QCOMPARE(proj()->timeline().markers.size(), 2);
        QVERIFY(std::abs(proj()->timeline().markers[0].time - 2.0) < 0.1 && std::abs(proj()->timeline().markers[1].time - 4.0) < 0.1);
        panel->detectScenes(true);                                     // then cut
        QTRY_VERIFY_WITH_TIMEOUT(!panel->isBusy(), 20000);
        QTRY_COMPARE_WITH_TIMEOUT(proj()->timeline().video[0].clips.size(), 3, 5000);
    }

    void rendersSensibleScreenshot() {
        load();
        w->program()->seek(1.0);
        QTest::qWait(400);
        const QImage shot = w->grab().toImage();
        QVERIFY(shot.width() >= 1400);
        // the program monitor shows the red clip: find a strongly red pixel somewhere on screen
        bool red = false;
        for (int y = 0; y < shot.height() && !red; y += 4)
            for (int x = 0; x < shot.width(); x += 4) {
                const QColor c(shot.pixel(x, y));
                if (c.red() > 200 && c.green() < 60 && c.blue() < 60) { red = true; break; }
            }
        QVERIFY(red);
    }
};

QTEST_MAIN(UiTests)
#include "test_ui.moc"
