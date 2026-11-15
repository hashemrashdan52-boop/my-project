#include "Renderer.h"

#include "PixelEffects.h"

#include <QFont>
#include <QFontMetricsF>
#include <algorithm>
#include <cmath>

namespace hr {

double fadeFactor(double local, double duration, double fadeIn, double fadeOut) {
    double f = 1.0;
    if (fadeIn > 1e-6) f = std::min(f, local / fadeIn);
    if (fadeOut > 1e-6) f = std::min(f, (duration - local) / fadeOut);
    return f < 0 ? 0 : (f > 1 ? 1 : f);
}

static const Clip* clipAt(const Track& tr, double t) {
    for (const Clip& c : tr.clips)
        if (t >= c.start - 1e-6 && t < c.end() - 1e-6) return &c;
    return nullptr;
}

// Where the picture of a clip sits at `local` seconds into it: the fixed Motion values plus the movement that
// builds up over the length of the clip (slow zoom / drift).
struct MotionPose {
    double scale = 1.0;
    double dx = 0, dy = 0;   // output pixels from the centre
    double rotation = 0;
};

static MotionPose motionPose(const EffectInstance* mo, double local, double duration, QSize out) {
    MotionPose m;
    if (!mo) return m;
    const double p = duration > 1e-9 ? std::clamp(local / duration, 0.0, 1.0) : 0.0;
    m.scale = std::max(0.0, mo->num("scale") / 100.0 * (1.0 + mo->num("zoom_amount") / 100.0 * p));
    m.dx = (mo->num("position_x") + mo->num("pan_x") * p) / 100.0 * out.width();
    m.dy = (mo->num("position_y") + mo->num("pan_y") * p) / 100.0 * out.height();
    m.rotation = mo->num("rotation");
    return m;
}

QImage Renderer::render(const Timeline& tl, double t, QSize out) {
    QImage canvas(out, QImage::Format_RGB32);
    canvas.fill(Qt::black);
    QPainter p(&canvas);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    p.setRenderHint(QPainter::Antialiasing, true);
    for (int ti = 0; ti < tl.video.size(); ++ti) {
        const Track& tr = tl.video[ti];
        if (!tr.enabled) continue;
        const Clip* c = clipAt(tr, t);
        if (!c || !c->enabled) continue;
        drawClip(p, *c, ti, t, out);
    }
    p.end();
    return canvas;
}

void Renderer::drawOffline(QPainter& p, const Clip& c, QSize out) {
    p.save();
    p.setOpacity(1);
    p.fillRect(QRect(QPoint(0, 0), out), QColor("#7a1f2b"));
    QFont f;
    f.setPixelSize(std::max(12, out.height() / 14));
    f.setBold(true);
    p.setFont(f);
    p.setPen(Qt::white);
    p.drawText(QRect(QPoint(0, 0), out), Qt::AlignCenter, QString("Media Offline\n%1").arg(c.name));
    p.restore();
}

void Renderer::drawTitle(QPainter& p, const Clip& c, double local, QSize out) {
    const EffectInstance* tx = c.effect("text");
    if (!tx) return;
    const EffectInstance* mo = c.effect("motion");
    const EffectInstance* op = c.effect("opacity");
    const double opac = (op ? op->num("opacity") : 100.0) / 100.0 *
                        fadeFactor(local, c.duration, op ? op->num("fade_in") : 0, op ? op->num("fade_out") : 0);
    if (opac <= 0) return;

    QFont f(tx->text("font"));
    f.setPixelSize(std::max(4, int(std::lround(tx->num("size") * out.height() / 1080.0))));
    f.setBold(tx->flag("bold"));
    f.setItalic(tx->flag("italic"));
    const QString text = tx->text("text");
    const QString align = tx->text("align");
    const int flags = (align == "Left" ? Qt::AlignLeft : (align == "Right" ? Qt::AlignRight : Qt::AlignHCenter)) |
                      Qt::TextWordWrap;
    const QFontMetricsF fm(f);
    QRectF bounds = fm.boundingRect(QRectF(0, 0, out.width() * 0.9, 100000), flags, text);
    bounds.moveCenter(QPointF(0, 0));

    const MotionPose pose = motionPose(mo, local, c.duration, out);
    p.save();
    p.setOpacity(opac);
    p.translate(out.width() / 2.0 + pose.dx, out.height() / 2.0 + pose.dy);
    if (mo) {
        p.rotate(pose.rotation);
        p.scale(pose.scale, pose.scale);
    }
    p.setFont(f);
    if (tx->flag("shadow")) {
        p.setPen(QColor(0, 0, 0, 160));
        const double o = std::max(1.0, f.pixelSize() / 22.0);
        p.drawText(bounds.translated(o, o), flags, text);
    }
    p.setPen(QColor(tx->text("color")));
    p.drawText(bounds, flags, text);
    p.restore();
}

void Renderer::drawClip(QPainter& p, const Clip& c, int trackIndex, double t, QSize out) {
    const double local = t - c.start;
    if (c.kind == ClipKind::Title) {
        drawTitle(p, c, local, out);
        return;
    }
    const EffectInstance* mo = c.effect("motion");
    const EffectInstance* op = c.effect("opacity");
    const double opac = (op ? op->num("opacity") : 100.0) / 100.0 *
                        fadeFactor(local, c.duration, op ? op->num("fade_in") : 0, op ? op->num("fade_out") : 0);
    if (opac <= 0) return;
    const MotionPose pose = motionPose(mo, local, c.duration, out);
    const double userScale = mo ? pose.scale : 1.0;
    if (userScale <= 0) return;
    const double userRot = pose.rotation;
    const double px = pose.dx;
    const double py = pose.dy;

    // geometry of the source -------------------------------------------------
    QImage img;
    QSizeF native;
    int rot = 0;
    const double srcT = c.srcIn + local * c.speed;
    if (c.mediaKind == MediaKind::Image) {
        const int want = int(out.width() * userScale);
        img = m_pool->still(c.path, std::max(16, want));
        native = img.size();
        if (!img.isNull()) {
            // stills are cached at several mip sizes: draw them at their original size
            // by reporting the biggest mip as "native"
            native = m_pool->still(c.path, 1 << 20).size();
        }
    } else {
        VideoDecoder* dec = m_pool->video(c.path + "#v" + QString::number(trackIndex), c.path);
        if (!dec) { drawOffline(p, c, out); return; }
        native = QSizeF(dec->nativeSize());
        rot = dec->rotation();
        const bool swapped = rot % 180 != 0;
        const QSizeF disp = swapped ? QSizeF(native.height(), native.width()) : native;
        const double fit = std::min(out.width() / disp.width(), out.height() / disp.height());
        const double need = std::max(1.0, fit * userScale);  // decoded px needed per source px (<=1 => downscale)
        QSize target = QSize(int(std::ceil(native.width() * std::min(1.0, need))),
                             int(std::ceil(native.height() * std::min(1.0, need))));
        img = dec->frameAt(srcT, target);
    }
    if (img.isNull() || native.isEmpty()) {
        if (m_pool->isOffline(c.path) || img.isNull()) drawOffline(p, c, out);
        return;
    }

    const bool swapped = rot % 180 != 0;
    const QSizeF disp = swapped ? QSizeF(native.height(), native.width()) : native;
    const double fit = std::min(out.width() / disp.width(), out.height() / disp.height());
    const double imgToOut = fit * userScale * (native.width() / img.width());

    // effects -----------------------------------------------------------------
    QRectF srcRect(0, 0, img.width(), img.height());
    QRectF dstRect(-native.width() / 2, -native.height() / 2, native.width(), native.height());
    bool mirrorH = false, mirrorV = false;
    bool needCopy = false;
    for (const EffectInstance& e : c.effects)
        if (e.enabled && e.type != "motion" && e.type != "opacity") { needCopy = true; break; }
    if (needCopy && img.format() != QImage::Format_ARGB32_Premultiplied && img.format() != QImage::Format_RGB32)
        img = img.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    bool copied = false;
    auto work = [&]() -> QImage& {
        if (!copied) { img = img.copy(); copied = true; }
        return img;
    };
    const double outScale = out.height() / 1080.0;
    for (const EffectInstance& e : c.effects) {
        if (!e.enabled) continue;
        if (e.type == "brightness_contrast") fx::brightnessContrast(work(), e.num("brightness"), e.num("contrast"));
        else if (e.type == "color_correction")
            fx::colorCorrection(work(), e.num("exposure"), e.num("saturation"), e.num("temperature"), e.num("tint"));
        else if (e.type == "black_white") fx::blackWhite(work(), e.num("amount"));
        else if (e.type == "invert") fx::invert(work(), e.num("amount"));
        else if (e.type == "gaussian_blur") fx::gaussianBlur(work(), e.num("blurriness") * outScale / std::max(1e-3, imgToOut));
        else if (e.type == "sharpen") fx::sharpen(work(), e.num("amount"), 1.2 * outScale / std::max(1e-3, imgToOut));
        else if (e.type == "vignette") fx::vignette(work(), e.num("amount"), e.num("size"));
        else if (e.type == "mirror") { mirrorH = e.flag("horizontal"); mirrorV = e.flag("vertical"); }
        else if (e.type == "crop") {
            const double l = e.num("left") / 100, tp = e.num("top") / 100, r = e.num("right") / 100, b = e.num("bottom") / 100;
            if (l + r >= 0.999 || tp + b >= 0.999) return;
            srcRect = QRectF(img.width() * l, img.height() * tp, img.width() * (1 - l - r), img.height() * (1 - tp - b));
            dstRect = QRectF(-native.width() / 2 + native.width() * l, -native.height() / 2 + native.height() * tp,
                             native.width() * (1 - l - r), native.height() * (1 - tp - b));
        }
    }

    p.save();
    p.setOpacity(opac);
    p.translate(out.width() / 2.0 + px, out.height() / 2.0 + py);
    p.rotate(userRot);
    p.scale(fit * userScale, fit * userScale);
    if (mirrorH || mirrorV) p.scale(mirrorH ? -1 : 1, mirrorV ? -1 : 1);
    if (rot) p.rotate(rot);
    p.drawImage(dstRect, img, srcRect);
    p.restore();
}

}  // namespace hr
