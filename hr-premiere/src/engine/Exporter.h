#pragma once
#include "core/Model.h"

#include <QStringList>
#include <QThread>
#include <atomic>

namespace hr {

struct ExportSettings {
    QString outputPath;
    QString format = "h264";   // h264, h265, prores, vp9, gif, mp3, wav
    int width = 1920, height = 1080;
    double fps = 30;
    int crf = 20;              // lower = better quality (h264 / h265 / vp9)
    int audioKbps = 192;
    double start = 0, end = 0; // seconds on the timeline
};

QString exportExtension(const QString& format);

// Renders the timeline frame by frame with the same compositor as the monitors and
// pipes the frames (plus a rendered audio mix) into the ffmpeg command line tool.
class Exporter : public QThread {
    Q_OBJECT
public:
    Exporter(const Timeline& tl, const ExportSettings& s, QObject* parent = nullptr);
    void cancel() { m_cancel = true; }

    static QString findFfmpeg();
    static bool hasEncoder(const QString& ffmpeg, const QString& name);

signals:
    void progress(double fraction, const QString& message);
    void exportFinished(bool ok, const QString& message);

protected:
    void run() override;

private:
    bool renderAudio(const QString& wavPath, double fromFraction, double toFraction);

    Timeline m_tl;
    ExportSettings m_s;
    std::atomic<bool> m_cancel{false};
};

}  // namespace hr
