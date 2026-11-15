#pragma once
#include "DecoderPool.h"
#include "core/Model.h"

#include <QImage>
#include <QPainter>
#include <QSize>

namespace hr {

// Composites all video tracks of a timeline at a given time into one frame.  The very
// same code produces the preview in the monitors and the frames written by the
// exporter, so what you see is what you get.
class Renderer {
public:
    explicit Renderer(DecoderPool* pool) : m_pool(pool) {}

    QImage render(const Timeline& tl, double t, QSize outSize);

private:
    void drawClip(QPainter& p, const Clip& c, int trackIndex, double t, QSize out);
    void drawTitle(QPainter& p, const Clip& c, double local, QSize out);
    void drawOffline(QPainter& p, const Clip& c, QSize out);

    DecoderPool* m_pool;
};

// Fade factor 0..1 for a clip-local time.
double fadeFactor(double local, double duration, double fadeIn, double fadeOut);

}  // namespace hr
