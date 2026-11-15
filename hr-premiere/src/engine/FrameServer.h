#pragma once
#include "Renderer.h"

#include <QImage>
#include <QMutex>
#include <QThread>
#include <QWaitCondition>
#include <memory>

namespace hr {

// Renders preview frames on a worker thread.  Only the most recent request is
// kept, so scrubbing never builds up a backlog.
class FrameServer : public QThread {
    Q_OBJECT
public:
    explicit FrameServer(QObject* parent = nullptr);
    ~FrameServer() override;

    void request(std::shared_ptr<const Timeline> tl, double t, QSize size);
    void shutdown();

signals:
    void frameReady(const QImage& image, double t);

protected:
    void run() override;

private:
    struct Req {
        std::shared_ptr<const Timeline> tl;
        double t = 0;
        QSize size;
    };
    QMutex m_mutex;
    QWaitCondition m_cond;
    Req m_pending;
    bool m_has = false;
    bool m_quit = false;
};

}  // namespace hr
