#include "Whisper.h"

#include "Transcript.h"
#include "engine/Exporter.h"

#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QUrl>

namespace hr::ai {

// ===================================================================== models
static QStringList hfUrls(const QString& repo, const QString& file) {
    return {QString("https://huggingface.co/%1/resolve/main/%2").arg(repo, file),
            QString("https://hf-mirror.com/%1/resolve/main/%2").arg(repo, file)};
}

QVector<ModelInfo> AiModels::models() {
    const QString repo = "ggerganov/whisper.cpp";
    return {
        {"tiny", "Fastest – Tiny (75 MB)", "ggml-tiny.bin", 60ll << 20, hfUrls(repo, "ggml-tiny.bin")},
        {"base", "Fast – Base (142 MB)", "ggml-base.bin", 120ll << 20, hfUrls(repo, "ggml-base.bin")},
        {"small", "Good – Small (466 MB)", "ggml-small.bin", 400ll << 20, hfUrls(repo, "ggml-small.bin")},
        {"turbo", "Best – Large v3 Turbo (574 MB, slow)", "ggml-large-v3-turbo-q5_0.bin", 450ll << 20,
         hfUrls(repo, "ggml-large-v3-turbo-q5_0.bin")},
    };
}

ModelInfo AiModels::modelById(const QString& id) {
    for (const ModelInfo& m : models())
        if (m.id == id) return m;
    return {};
}

ModelInfo AiModels::vadModel() {
    return {"vad", "Voice activity (small)", "ggml-silero-v5.1.2.bin", 400ll << 10, hfUrls("ggml-org/whisper-vad", "ggml-silero-v5.1.2.bin")};
}

QString AiModels::dir() {
    const QString env = qEnvironmentVariable("HR_AI_DIR");
    const QString d = env.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/ai" : env;
    QDir().mkpath(d);
    return d;
}

QString AiModels::pathOf(const ModelInfo& m) { return QDir(dir()).filePath(m.file); }

bool AiModels::isInstalled(const ModelInfo& m) {
    const QFileInfo fi(pathOf(m));
    return fi.exists() && fi.size() >= m.minBytes;
}

AiModels::AiModels(QObject* parent) : QObject(parent) {}

void AiModels::download(const ModelInfo& m) {
    if (m_reply) return;
    m_model = m;
    m_urlIndex = 0;
    m_cancelled = false;
    m_lastError.clear();
    startNext();
}

void AiModels::cancel() {
    m_cancelled = true;
    if (m_reply) m_reply->abort();
}

void AiModels::fail(const QString& why) {
    QFile::remove(pathOf(m_model) + ".part");
    emit finished(false, why);
}

void AiModels::startNext() {
    if (m_urlIndex >= m_model.urls.size()) {
        fail(QString("Could not download the speech model \"%1\". %2\nCheck the internet connection and try again.")
                 .arg(m_model.title, m_lastError));
        return;
    }
    const QUrl url(m_model.urls[m_urlIndex]);
    m_file.setFileName(pathOf(m_model) + ".part");
    if (!m_file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        fail("Could not write to " + m_file.fileName() + ": " + m_file.errorString());
        return;
    }
    QNetworkRequest req(url);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setHeader(QNetworkRequest::UserAgentHeader, "HRPremiere");
    req.setTransferTimeout(30000);
    m_reply = m_nam.get(req);
    connect(m_reply, &QNetworkReply::readyRead, this, [this] {
        if (m_reply) m_file.write(m_reply->readAll());
    });
    connect(m_reply, &QNetworkReply::downloadProgress, this, [this](qint64 r, qint64 t) { emit progress(r, t); });
    connect(m_reply, &QNetworkReply::finished, this, [this] {
        QNetworkReply* reply = m_reply;
        m_reply = nullptr;
        m_file.write(reply->readAll());
        m_file.close();
        const QNetworkReply::NetworkError err = reply->error();
        const QString errText = reply->errorString();
        reply->deleteLater();
        if (m_cancelled) {
            fail("Download cancelled.");
            return;
        }
        const qint64 size = QFileInfo(m_file.fileName()).size();
        if (err != QNetworkReply::NoError || size < m_model.minBytes) {
            m_lastError = err != QNetworkReply::NoError ? errText : QString("The file was incomplete (%1 bytes).").arg(size);
            ++m_urlIndex;
            startNext();
            return;
        }
        QFile::remove(pathOf(m_model));
        if (!QFile::rename(m_file.fileName(), pathOf(m_model))) {
            fail("Could not save the model file.");
            return;
        }
        emit finished(true, QString());
    });
}

// ===================================================================== transcriber
Transcriber::Transcriber(QObject* parent) : QObject(parent) { qRegisterMetaType<QVector<hr::TranscriptSegment>>(); }

Transcriber::~Transcriber() {
    if (m_proc) {
        m_proc->disconnect(this);
        m_proc->kill();
        m_proc->waitForFinished(2000);
    }
}

bool Transcriber::supportsWhisper(const QString& ffmpeg, QString* detail) {
    static QHash<QString, bool> cache;
    if (cache.contains(ffmpeg)) return cache.value(ffmpeg);
    QProcess p;
    p.start(ffmpeg, {"-hide_banner", "-filters"});
    const bool ran = p.waitForFinished(15000);
    const QString out = QString::fromUtf8(p.readAllStandardOutput());
    const bool ok = ran && out.contains(QRegularExpression(R"(\swhisper\s)"));
    if (!ok && detail)
        *detail = "The ffmpeg that is installed does not include the Whisper speech filter (FFmpeg 8 or newer is needed). "
                  "The Windows version of HR Premiere includes it.";
    cache.insert(ffmpeg, ok);
    return ok;
}

void Transcriber::start(const Request& r) {
    if (isRunning()) return;
    m_req = r;
    m_cancelled = false;
    m_log.clear();
    const QString ffmpeg = Exporter::findFfmpeg();
    if (ffmpeg.isEmpty()) {
        finish(false, {}, "ffmpeg was not found. Put ffmpeg next to HRPremiere or add it to your PATH.");
        return;
    }
    QString why;
    if (!supportsWhisper(ffmpeg, &why)) {
        finish(false, {}, why);
        return;
    }
    if (!AiModels::isInstalled(r.model)) {
        finish(false, {}, "The speech model is not installed yet.");
        return;
    }
    m_outName = QString("transcript_%1.srt").arg(QString(r.mediaId).remove(QRegularExpression("[^A-Za-z0-9_-]")));
    launch(r.useVad && AiModels::isInstalled(AiModels::vadModel()));
}

void Transcriber::launch(bool withVad) {
    m_vad = withVad;
    m_buf.clear();
    QFile::remove(QDir(AiModels::dir()).filePath(m_outName));
    // relative file names only: no drive letters or backslashes inside the filter string
    QString filter = QString("whisper=model=%1:language=%2:queue=20:use_gpu=false:destination=%3:format=srt")
                         .arg(m_req.model.file, m_req.language.isEmpty() ? "auto" : m_req.language, m_outName);
    if (withVad) filter += QString(":vad_model=%1").arg(AiModels::vadModel().file);
    if (!m_req.preFilter.trimmed().isEmpty()) filter = m_req.preFilter.trimmed() + "," + filter;
    const QStringList args = {"-hide_banner", "-nostdin", "-y", "-i", m_req.mediaPath, "-vn", "-map", "0:a:0", "-af", filter,
                              "-progress", "pipe:1", "-nostats", "-f", "null", "-"};
    if (m_proc) m_proc->deleteLater();
    m_proc = new QProcess(this);
    m_proc->setWorkingDirectory(AiModels::dir());
    connect(m_proc, &QProcess::readyReadStandardOutput, this, &Transcriber::onStdout);
    connect(m_proc, &QProcess::readyReadStandardError, this, &Transcriber::onStderr);
    connect(m_proc, &QProcess::finished, this, &Transcriber::onFinished);
    m_proc->start(Exporter::findFfmpeg(), args);
    if (!m_proc->waitForStarted(10000)) finish(false, {}, "Could not start ffmpeg: " + m_proc->errorString());
}

void Transcriber::onStdout() {
    m_buf += m_proc->readAllStandardOutput();
    int nl;
    while ((nl = m_buf.indexOf('\n')) >= 0) {
        const QByteArray line = m_buf.left(nl).trimmed();
        m_buf.remove(0, nl + 1);
        if (line.startsWith("out_time_us=") || line.startsWith("out_time_ms=")) {
            const double secs = line.mid(line.indexOf('=') + 1).toDouble() / 1e6;
            if (m_req.duration > 0) emit progress(std::max(0.0, std::min(0.99, secs / m_req.duration)));
        }
    }
}

void Transcriber::onStderr() {
    m_log += QString::fromUtf8(m_proc->readAllStandardError());
    if (m_log.size() > 6000) m_log = m_log.right(6000);
}

void Transcriber::cancel() {
    m_cancelled = true;
    if (isRunning()) m_proc->kill();
}

void Transcriber::onFinished(int code, QProcess::ExitStatus status) {
    onStderr();
    if (m_cancelled) {
        finish(false, {}, "Cancelled.");
        return;
    }
    if (status != QProcess::NormalExit || code != 0) {
        if (m_vad) {                // maybe the VAD model is the problem: try again without it
            launch(false);
            return;
        }
        QString tail = m_log.trimmed();
        if (tail.size() > 600) tail = "…" + tail.right(600);
        finish(false, {}, "Speech recognition failed.\n" + tail);
        return;
    }
    QFile f(QDir(AiModels::dir()).filePath(m_outName));
    QVector<TranscriptSegment> segs;
    if (f.open(QIODevice::ReadOnly)) {
        segs = parseSrt(QString::fromUtf8(f.readAll()));
        f.close();
        f.remove();
    }
    emit progress(1.0);
    finish(true, segs, segs.isEmpty() ? "No speech was found." : QString("%1 sentences").arg(segs.size()));
}

void Transcriber::finish(bool ok, const QVector<TranscriptSegment>& segs, const QString& msg) {
    emit finished(ok, segs, msg);
}

}  // namespace hr::ai
