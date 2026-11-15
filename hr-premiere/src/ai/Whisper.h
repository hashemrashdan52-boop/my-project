#pragma once
#include "core/Model.h"

#include <QFile>
#include <QNetworkAccessManager>
#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <QVector>

class QNetworkReply;

namespace hr::ai {

// A speech-recognition model (whisper.cpp "ggml" format).  They are public downloads: no account, no key.
struct ModelInfo {
    QString id;               // "tiny", "base", "small", "turbo"
    QString title;            // shown in the UI
    QString file;             // file name inside the models folder
    qint64 minBytes = 0;      // a smaller file is a broken / partial download
    QStringList urls;         // tried in order (mirrors)
    bool valid() const { return !id.isEmpty(); }
};

class AiModels : public QObject {
    Q_OBJECT
public:
    explicit AiModels(QObject* parent = nullptr);

    static QVector<ModelInfo> models();
    static ModelInfo modelById(const QString& id);       // invalid ModelInfo if unknown
    static ModelInfo vadModel();                         // small voice-activity model (skips silence / music)
    static QString dir();                                // where the models live (HR_AI_DIR overrides)
    static QString pathOf(const ModelInfo& m);
    static bool isInstalled(const ModelInfo& m);

    void download(const ModelInfo& m);                   // async; follows redirects, tries every mirror
    void cancel();
    bool isDownloading() const { return m_reply != nullptr; }

signals:
    void progress(qint64 received, qint64 total);
    void finished(bool ok, const QString& message);

private:
    void startNext();
    void fail(const QString& why);

    QNetworkAccessManager m_nam;
    QNetworkReply* m_reply = nullptr;
    ModelInfo m_model;
    int m_urlIndex = 0;
    QFile m_file;
    QString m_lastError;
    bool m_cancelled = false;
};

// Turns the speech of a media file into text with the Whisper filter of the bundled ffmpeg.
class Transcriber : public QObject {
    Q_OBJECT
public:
    struct Request {
        QString mediaId;
        QString mediaPath;
        double duration = 0;          // seconds, only used for the progress bar
        QString language = "auto";    // "auto", "ar", "en", ...
        ModelInfo model;
        bool useVad = true;           // retried without if the VAD model cannot be used
        QString preFilter;            // ffmpeg audio filters applied before the model hears the sound (see Cleanup.h)
    };

    explicit Transcriber(QObject* parent = nullptr);
    ~Transcriber() override;

    // Does this ffmpeg have the "whisper" filter (FFmpeg 8 or newer)?
    static bool supportsWhisper(const QString& ffmpeg, QString* detail = nullptr);

    void start(const Request& r);
    void cancel();
    bool isRunning() const { return m_proc && m_proc->state() != QProcess::NotRunning; }

signals:
    void progress(double fraction);
    void finished(bool ok, const QVector<hr::TranscriptSegment>& segments, const QString& message);

private slots:
    void onStdout();
    void onStderr();
    void onFinished(int code, QProcess::ExitStatus status);

private:
    void launch(bool withVad);
    void finish(bool ok, const QVector<TranscriptSegment>& segs, const QString& msg);

    Request m_req;
    QProcess* m_proc = nullptr;
    bool m_vad = false;
    bool m_cancelled = false;
    QString m_outName;
    QString m_log;
    QByteArray m_buf;
};

}  // namespace hr::ai

Q_DECLARE_METATYPE(QVector<hr::TranscriptSegment>)
