#include "TranscribeCli.h"

#include "Transcript.h"
#include "Whisper.h"
#include "media/MediaProbe.h"

#include <QEventLoop>
#include <QFile>
#include <QTextStream>

namespace hr::ai {

int runTranscribeCli(const QString& mediaPath, const QString& modelId, const QString& language, const QString& outPath) {
    QTextStream out(stdout);
    QFile logFile(outPath.isEmpty() ? QString() : outPath + ".log");   // the Windows exe has no console: keep a log
    if (!outPath.isEmpty()) logFile.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text);
    auto say = [&](const QString& s) {
        out << s << Qt::endl;
        if (logFile.isOpen()) {
            logFile.write(s.toUtf8() + "\n");
            logFile.flush();
        }
    };

    MediaItem item;
    QString err;
    if (!probeMedia(mediaPath, item, &err) || !item.hasAudio) {
        say("error: cannot read audio from " + mediaPath + " " + err);
        return 2;
    }
    const ModelInfo model = AiModels::modelById(modelId);
    if (!model.valid()) {
        say("error: unknown model " + modelId);
        return 2;
    }

    // models first (the voice-activity model is optional)
    for (const ModelInfo& m : {model, AiModels::vadModel()}) {
        if (AiModels::isInstalled(m)) continue;
        say("downloading " + m.title);
        AiModels models;
        QEventLoop loop;
        bool ok = false;
        QString msg;
        qint64 lastPct = -1;
        QObject::connect(&models, &AiModels::progress, &loop, [&](qint64 r, qint64 t) {
            const qint64 pct = t > 0 ? r * 100 / t : 0;
            if (pct != lastPct && pct % 10 == 0) say(QString("  %1%").arg(pct));
            lastPct = pct;
        });
        QObject::connect(&models, &AiModels::finished, &loop, [&](bool o, const QString& s) { ok = o; msg = s; loop.quit(); });
        models.download(m);
        loop.exec();
        if (!ok && m.id == model.id) {
            say("error: " + msg);
            return 2;
        }
    }

    Transcriber tr;
    Transcriber::Request req;
    req.mediaId = "cli";
    req.mediaPath = mediaPath;
    req.duration = item.duration;
    req.language = language.isEmpty() ? "auto" : language;
    req.model = model;
    QEventLoop loop;
    bool ok = false;
    QVector<TranscriptSegment> segs;
    QString msg;
    QObject::connect(&tr, &Transcriber::finished, &loop, [&](bool o, const QVector<TranscriptSegment>& s, const QString& m) {
        ok = o;
        segs = s;
        msg = m;
        loop.quit();
    });
    say("transcribing " + mediaPath);
    tr.start(req);
    if (tr.isRunning()) loop.exec();
    else if (msg.isEmpty() && !ok) loop.processEvents();
    if (!ok) {
        say("error: " + msg);
        return 2;
    }
    if (!outPath.isEmpty()) {
        QFile f(outPath);
        if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) f.write(formatSrt(segs).toUtf8());
    }
    for (const TranscriptSegment& s : segs) say(QString("[%1 - %2] %3").arg(s.start, 0, 'f', 2).arg(s.end, 0, 'f', 2).arg(s.text));
    return segs.isEmpty() ? 3 : 0;
}

}  // namespace hr::ai
