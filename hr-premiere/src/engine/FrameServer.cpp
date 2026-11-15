#include "FrameServer.h"

namespace hr {

FrameServer::FrameServer(QObject* parent) : QThread(parent) {}

FrameServer::~FrameServer() { shutdown(); }

void FrameServer::shutdown() {
    {
        QMutexLocker l(&m_mutex);
        m_quit = true;
    }
    m_cond.wakeAll();
    if (isRunning()) wait();
}

void FrameServer::request(std::shared_ptr<const Timeline> tl, double t, QSize size) {
    {
        QMutexLocker l(&m_mutex);
        m_pending = {std::move(tl), t, size};
        m_has = true;
    }
    m_cond.wakeOne();
    if (!isRunning()) start(QThread::HighPriority);
}

void FrameServer::run() {
    DecoderPool pool;
    Renderer renderer(&pool);
    for (;;) {
        Req req;
        {
            QMutexLocker l(&m_mutex);
            while (!m_has && !m_quit) m_cond.wait(&m_mutex);
            if (m_quit) break;
            req = m_pending;
            m_has = false;
        }
        if (!req.tl || req.size.isEmpty()) continue;
        QImage img = renderer.render(*req.tl, req.t, req.size);
        emit frameReady(img, req.t);
    }
}

}  // namespace hr
