#include "core/I18n.h"
#include "VoiceRecorder.h"

#include <QAudioDevice>
#include <QMediaDevices>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace hr {

VoiceRecorder::VoiceRecorder(QObject* parent) : QObject(parent) {}

VoiceRecorder::~VoiceRecorder() {
    if (isRecording()) stop();
}

bool VoiceRecorder::hasMicrophone() { return !QMediaDevices::defaultAudioInput().isNull(); }

QString VoiceRecorder::deviceName() { return QMediaDevices::defaultAudioInput().description(); }

bool VoiceRecorder::start(const QString& wavPath, QString* error) {
    if (isRecording()) return false;
    auto fail = [&](const QString& msg) {
        if (error) *error = msg;
        return false;
    };
    const QAudioDevice dev = QMediaDevices::defaultAudioInput();
    if (dev.isNull()) return fail(TR("No microphone was found."));

    QAudioFormat fmt;
    fmt.setSampleRate(48000);
    fmt.setChannelCount(1);
    fmt.setSampleFormat(QAudioFormat::Int16);
    if (!dev.isFormatSupported(fmt)) fmt = dev.preferredFormat();
    const auto sf = fmt.sampleFormat();
    if (sf != QAudioFormat::Int16 && sf != QAudioFormat::Float && sf != QAudioFormat::Int32 && sf != QAudioFormat::UInt8)
        return fail(TR("The microphone uses a sample format that is not supported."));
    m_format = fmt;

    m_file.setFileName(wavPath);
    if (!m_file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return fail(TR("Could not create ") + wavPath + ": " + m_file.errorString());
    m_dataBytes = 0;
    writeHeader();   // placeholder sizes, fixed in stop()

    m_source = std::make_unique<QAudioSource>(dev, m_format);
    m_source->setBufferSize(m_format.bytesPerFrame() * m_format.sampleRate() / 5);   // 200 ms
    m_io = m_source->start();
    if (!m_io || m_source->error() != QAudio::NoError) {
        m_source.reset();
        m_io = nullptr;
        m_file.close();
        QFile::remove(wavPath);
        return fail(TR("The microphone could not be opened. Check that it is connected and that Windows allows desktop apps to use it (Settings > Privacy > Microphone)."));
    }
    connect(m_io, &QIODevice::readyRead, this, &VoiceRecorder::onReadyRead);
    connect(m_source.get(), &QAudioSource::stateChanged, this, [this](QAudio::State st) {
        if (st == QAudio::StoppedState && m_source && m_source->error() != QAudio::NoError)
            emit failed(TR("The microphone stopped unexpectedly."));
    });
    return true;
}

void VoiceRecorder::onReadyRead() {
    if (!m_io) return;
    const QByteArray raw = m_io->readAll();
    if (raw.isEmpty()) return;
    const int bps = m_format.bytesPerSample();
    const qint64 n = raw.size() / bps;
    QByteArray out(int(n * 2), Qt::Uninitialized);
    auto* dst = reinterpret_cast<int16_t*>(out.data());
    float peak = 0;
    switch (m_format.sampleFormat()) {
    case QAudioFormat::Int16:
        std::memcpy(dst, raw.constData(), size_t(n) * 2);
        break;
    case QAudioFormat::Float: {
        const auto* s = reinterpret_cast<const float*>(raw.constData());
        for (qint64 i = 0; i < n; ++i) dst[i] = int16_t(std::lround(std::max(-1.0f, std::min(1.0f, s[i])) * 32767.0f));
        break;
    }
    case QAudioFormat::Int32: {
        const auto* s = reinterpret_cast<const int32_t*>(raw.constData());
        for (qint64 i = 0; i < n; ++i) dst[i] = int16_t(s[i] >> 16);
        break;
    }
    case QAudioFormat::UInt8: {
        const auto* s = reinterpret_cast<const uint8_t*>(raw.constData());
        for (qint64 i = 0; i < n; ++i) dst[i] = int16_t((int(s[i]) - 128) << 8);
        break;
    }
    default: return;
    }
    for (qint64 i = 0; i < n; ++i) peak = std::max(peak, std::abs(dst[i]) / 32768.0f);
    m_file.write(out);
    m_dataBytes += out.size();
    emit level(peak);
}

double VoiceRecorder::seconds() const {
    if (!m_format.isValid()) return 0;
    const double bytesPerSecond = double(m_format.sampleRate()) * m_format.channelCount() * 2.0;
    return bytesPerSecond > 0 ? double(m_dataBytes) / bytesPerSecond : 0;
}

static void putLE(QByteArray& b, quint32 v, int bytes) {
    for (int i = 0; i < bytes; ++i) b.append(char((v >> (8 * i)) & 0xff));
}

void VoiceRecorder::writeHeader() {
    const quint32 channels = quint32(m_format.channelCount());
    const quint32 rate = quint32(m_format.sampleRate());
    QByteArray h;
    h += "RIFF";
    putLE(h, quint32(36 + m_dataBytes), 4);
    h += "WAVEfmt ";
    putLE(h, 16, 4);
    putLE(h, 1, 2);                      // PCM
    putLE(h, channels, 2);
    putLE(h, rate, 4);
    putLE(h, rate * channels * 2, 4);
    putLE(h, channels * 2, 2);
    putLE(h, 16, 2);
    h += "data";
    putLE(h, quint32(m_dataBytes), 4);
    m_file.seek(0);
    m_file.write(h);
}

double VoiceRecorder::stop() {
    if (!m_source) return 0;
    onReadyRead();            // drain what is left (the device returned by start() dies with stop())
    m_io = nullptr;
    m_source->stop();
    m_source.reset();
    writeHeader();
    m_file.close();
    return seconds();
}

}  // namespace hr
