#include "Exporter.h"

#include "AudioMixer.h"
#include "Renderer.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <cmath>

namespace hr {

QString exportExtension(const QString& f) {
    if (f == "prores") return "mov";
    if (f == "vp9") return "webm";
    if (f == "gif") return "gif";
    if (f == "mp3") return "mp3";
    if (f == "wav") return "wav";
    return "mp4";
}

Exporter::Exporter(const Timeline& tl, const ExportSettings& s, QObject* parent)
    : QThread(parent), m_tl(tl), m_s(s) {}

QString Exporter::findFfmpeg() {
    const QString env = qEnvironmentVariable("HR_FFMPEG");
    if (!env.isEmpty() && QFileInfo::exists(env)) return env;
#ifdef Q_OS_WIN
    const QString local = QCoreApplication::applicationDirPath() + "/ffmpeg.exe";
#else
    const QString local = QCoreApplication::applicationDirPath() + "/ffmpeg";
#endif
    if (QFileInfo::exists(local)) return local;
    return QStandardPaths::findExecutable("ffmpeg");
}

bool Exporter::hasEncoder(const QString& ffmpeg, const QString& name) {
    QProcess p;
    p.start(ffmpeg, {"-hide_banner", "-encoders"});
    if (!p.waitForFinished(8000)) return false;
    const QString out = QString::fromUtf8(p.readAllStandardOutput());
    return out.contains(QRegularExpression("\\s" + QRegularExpression::escape(name) + "\\s"));
}

static void putLE(QByteArray& b, quint32 v, int bytes) {
    for (int i = 0; i < bytes; ++i) b.append(char((v >> (8 * i)) & 0xff));
}

bool Exporter::renderAudio(const QString& wavPath, double from, double to) {
    QFile f(wavPath);
    if (!f.open(QIODevice::WriteOnly)) return false;
    const qint64 total = qint64(std::ceil((m_s.end - m_s.start) * AudioMixer::kRate));
    const quint32 dataBytes = quint32(total * 4);
    QByteArray hdr;
    hdr += "RIFF";
    putLE(hdr, 36 + dataBytes, 4);
    hdr += "WAVEfmt ";
    putLE(hdr, 16, 4);
    putLE(hdr, 1, 2);                          // PCM
    putLE(hdr, 2, 2);                          // stereo
    putLE(hdr, AudioMixer::kRate, 4);
    putLE(hdr, AudioMixer::kRate * 4, 4);
    putLE(hdr, 4, 2);
    putLE(hdr, 16, 2);
    hdr += "data";
    putLE(hdr, dataBytes, 4);
    f.write(hdr);

    DecoderPool pool;
    AudioMixer mixer(&pool);
    const int chunk = 8192;
    std::vector<float> buf(size_t(chunk) * 2);
    std::vector<int16_t> pcm(size_t(chunk) * 2);
    const qint64 startSample = qint64(std::llround(m_s.start * AudioMixer::kRate));
    for (qint64 done = 0; done < total; done += chunk) {
        if (m_cancel) return false;
        const int n = int(std::min<qint64>(chunk, total - done));
        mixer.render(m_tl, startSample + done, n, buf.data());
        for (int i = 0; i < n * 2; ++i) pcm[size_t(i)] = int16_t(std::lround(buf[size_t(i)] * 32767.0f));
        f.write(reinterpret_cast<const char*>(pcm.data()), qint64(n) * 4);
        emit progress(from + (to - from) * double(done + n) / double(total), "Rendering audio…");
    }
    f.close();
    return true;
}

void Exporter::run() {
    const QString ffmpeg = findFfmpeg();
    if (ffmpeg.isEmpty()) {
        emit exportFinished(false, "ffmpeg was not found. Put ffmpeg next to HRPremiere or add it to your PATH.");
        return;
    }
    const bool audioOnly = m_s.format == "mp3" || m_s.format == "wav";
    const bool wantAudio = m_s.format != "gif";
    const bool wantVideo = !audioOnly;
    m_s.end = std::max(m_s.end, m_s.start + 1.0 / m_tl.fps);
    const double duration = m_s.end - m_s.start;
    const qint64 frames = qint64(std::ceil(duration * m_s.fps - 1e-6));
    const int W = std::max(2, m_s.width & ~1), H = std::max(2, m_s.height & ~1);

    QTemporaryDir tmp;
    if (!tmp.isValid()) {
        emit exportFinished(false, "Could not create a temporary folder.");
        return;
    }
    const QString wav = tmp.filePath("mix.wav");
    if (wantAudio) {
        if (!renderAudio(wav, 0.0, wantVideo ? 0.1 : 0.9)) {
            emit exportFinished(false, m_cancel ? "Export cancelled." : "Could not render the audio mix.");
            return;
        }
    }

    QStringList args = {"-hide_banner", "-loglevel", "error", "-nostats", "-y"};
    if (wantVideo) {
        args << "-f" << "rawvideo" << "-pix_fmt" << "bgra" << "-video_size" << QString("%1x%2").arg(W).arg(H)
             << "-framerate" << QString::number(m_s.fps, 'f', 5) << "-i" << "pipe:0";
    }
    if (wantAudio) args << "-i" << wav;
    if (wantVideo && wantAudio) args << "-map" << "0:v" << "-map" << "1:a";

    const QString fmt = m_s.format;
    if (fmt == "h264") {
        if (hasEncoder(ffmpeg, "libx264"))
            args << "-c:v" << "libx264" << "-preset" << "medium" << "-crf" << QString::number(m_s.crf);
        else
            args << "-c:v" << "mpeg4" << "-q:v" << QString::number(std::max(2, m_s.crf / 5));
        args << "-pix_fmt" << "yuv420p" << "-c:a" << "aac" << "-b:a" << QString("%1k").arg(m_s.audioKbps)
             << "-movflags" << "+faststart";
    } else if (fmt == "h265") {
        args << "-c:v" << "libx265" << "-preset" << "medium" << "-crf" << QString::number(m_s.crf + 4)
             << "-tag:v" << "hvc1" << "-pix_fmt" << "yuv420p" << "-c:a" << "aac" << "-b:a"
             << QString("%1k").arg(m_s.audioKbps);
    } else if (fmt == "prores") {
        args << "-c:v" << "prores_ks" << "-profile:v" << "3" << "-pix_fmt" << "yuv422p10le" << "-c:a" << "pcm_s16le";
    } else if (fmt == "vp9") {
        args << "-c:v" << "libvpx-vp9" << "-crf" << QString::number(m_s.crf + 6) << "-b:v" << "0" << "-c:a"
             << "libopus" << "-b:a" << QString("%1k").arg(m_s.audioKbps);
    } else if (fmt == "gif") {
        args << "-filter_complex" << "[0:v]split[a][b];[a]palettegen=stats_mode=diff[p];[b][p]paletteuse=dither=bayer"
             << "-an";
    } else if (fmt == "mp3") {
        args << "-vn" << "-c:a" << "libmp3lame" << "-b:a" << QString("%1k").arg(m_s.audioKbps);
    } else if (fmt == "wav") {
        args << "-vn" << "-c:a" << "pcm_s16le";
    }
    if (wantVideo && wantAudio) args << "-shortest";
    args << m_s.outputPath;

    QProcess proc;
    proc.setProcessChannelMode(QProcess::MergedChannels);
    proc.start(ffmpeg, args);
    if (!proc.waitForStarted(10000)) {
        emit exportFinished(false, "Could not start ffmpeg: " + proc.errorString());
        return;
    }

    QString log;
    auto drain = [&] {
        log += QString::fromUtf8(proc.readAll());
        if (log.size() > 8000) log = log.right(8000);
    };

    if (wantVideo) {
        DecoderPool pool;
        Renderer renderer(&pool);
        QElapsedTimer timer;
        timer.start();
        const double base = wantAudio ? 0.1 : 0.0;
        for (qint64 i = 0; i < frames; ++i) {
            if (m_cancel) break;
            const double t = m_s.start + double(i) / m_s.fps;
            QImage img = renderer.render(m_tl, t, QSize(W, H));
            if (img.format() != QImage::Format_RGB32) img = img.convertToFormat(QImage::Format_RGB32);
            const qint64 want = img.sizeInBytes();
            if (proc.write(reinterpret_cast<const char*>(img.constBits()), want) != want) break;
            while (proc.bytesToWrite() > 0) {
                if (!proc.waitForBytesWritten(2000) && proc.state() != QProcess::Running) break;
                drain();
            }
            drain();
            if (proc.state() != QProcess::Running) break;
            if (i % 4 == 0) {
                const double frac = base + (1.0 - base) * double(i + 1) / double(frames);
                const double el = timer.elapsed() / 1000.0;
                const double eta = i > 0 ? el / double(i) * double(frames - i) : 0.0;
                emit progress(frac * 0.99, QString("Rendering frame %1 of %2   (about %3 s left)")
                                               .arg(i + 1).arg(frames).arg(int(std::ceil(eta))));
            }
        }
        proc.closeWriteChannel();
    }

    if (m_cancel) {
        proc.kill();
        proc.waitForFinished(3000);
        QFile::remove(m_s.outputPath);
        emit exportFinished(false, "Export cancelled.");
        return;
    }
    emit progress(0.995, "Finishing…");
    if (!proc.waitForFinished(120000)) proc.kill();
    drain();
    if (proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0) {
        QFile::remove(m_s.outputPath);
        emit exportFinished(false, "ffmpeg failed:\n" + log.trimmed());
        return;
    }
    emit progress(1.0, "Done");
    emit exportFinished(true, m_s.outputPath);
}

}  // namespace hr
