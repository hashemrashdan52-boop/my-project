#pragma once
#include "core/Model.h"

#include <QHash>
#include <QImage>
#include <QMutex>
#include <QSet>
#include <functional>
#include <QObject>
#include <QThreadPool>
#include <QVector>

namespace hr {

// Generated in the background for every imported file: thumbnails for the Project
// panel / timeline clips and an audio waveform.
struct MediaAsset {
    QImage poster;               // upright, ~320 px wide
    QVector<QImage> strip;       // evenly spaced thumbnails (upright, ~72 px tall)
    double stripStep = 0;        // seconds between strip thumbnails
    QVector<quint8> peaks100;    // |max| per 10 ms
    QVector<quint8> peaks10;     // per 100 ms
    QVector<quint8> peaks1;      // per 1 s
    bool thumbsReady = false;
    bool wavesReady = false;
};

// Loudness of a media file: the largest sample of every 10 ms, as a byte (0..255). Synchronous.
QVector<quint8> computePeaks100(const QString& path, double duration);

class MediaAssets : public QObject {
    Q_OBJECT
public:
    explicit MediaAssets(QObject* parent = nullptr);
    ~MediaAssets() override;

    void request(const MediaItem& item);          // no-op if already generated / running
    MediaAsset get(const QString& mediaId) const; // snapshot (cheap: implicitly shared)
    void forget(const QString& mediaId);

signals:
    void updated(const QString& mediaId);

private:
    void runThumbs(MediaItem item);
    void runWaveform(MediaItem item);

    mutable QMutex m_mutex;
    QHash<QString, MediaAsset> m_assets;
    QSet<QString> m_started;
    QThreadPool m_pool;
};

}  // namespace hr
