#include "TimelineWidget.h"

#include "Icons.h"
#include "Theme.h"
#include "core/EditOps.h"
#include "core/Util.h"
#include "engine/Renderer.h"

#include <QContextMenuEvent>
#include <QDragEnterEvent>
#include <QFontMetrics>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QUrl>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>

namespace hr {

static const char* kMediaMime = "application/x-hr-media";
static const char* kEffectMime = "application/x-hr-effect";
static const int kVideoH = 58, kAudioH = 48, kGap = 10, kClipHead = 16;

TimelineWidget::TimelineWidget(Project* project, MediaAssets* assets, QWidget* parent)
    : QWidget(parent), m_project(project), m_assets(assets) {
    setMouseTracking(true);
    setAcceptDrops(true);
    setFocusPolicy(Qt::StrongFocus);
    setMinimumHeight(140);

    m_hbar = new QScrollBar(Qt::Horizontal, this);
    m_vbar = new QScrollBar(Qt::Vertical, this);
    connect(m_hbar, &QScrollBar::valueChanged, this, [this](int v) {
        if (m_updatingBars) return;
        m_origin = v / 100.0;
        update();
    });
    connect(m_vbar, &QScrollBar::valueChanged, this, [this](int v) {
        if (m_updatingBars) return;
        m_vScroll = v;
        update();
    });
    connect(m_project, &Project::timelineChanged, this, &TimelineWidget::onTimelineChanged);
    connect(m_project, &Project::selectionChanged, this, QOverload<>::of(&TimelineWidget::update));
    connect(m_project, &Project::projectReset, this, [this] { m_origin = 0; m_vScroll = 0; update(); });
    connect(m_assets, &MediaAssets::updated, this, &TimelineWidget::onAssetsUpdated);
    layoutRows();
}

// ------------------------------------------------------------------ geometry
QRect TimelineWidget::contentRect() const {
    return QRect(headerW(), rulerH(), width() - headerW() - scrollBarSize(), height() - rulerH() - scrollBarSize());
}
QRect TimelineWidget::rulerRect() const { return QRect(headerW(), 0, width() - headerW() - scrollBarSize(), rulerH()); }

int TimelineWidget::contentHeight() const {
    const Timeline& tl = m_project->timeline();
    return tl.video.size() * kVideoH + kGap + tl.audio.size() * kAudioH;
}

void TimelineWidget::layoutRows() {
    m_rows.clear();
    const Timeline& tl = m_project->timeline();
    int y = rulerH() - m_vScroll;
    for (int i = tl.video.size() - 1; i >= 0; --i) {
        m_rows.push_back({TrackType::Video, i, y, kVideoH});
        y += kVideoH;
    }
    y += kGap;
    for (int i = 0; i < tl.audio.size(); ++i) {
        m_rows.push_back({TrackType::Audio, i, y, kAudioH});
        y += kAudioH;
    }
    m_targetV = std::max(0, std::min(m_targetV, int(tl.video.size()) - 1));
    m_targetA = std::max(0, std::min(m_targetA, int(tl.audio.size()) - 1));
}

int TimelineWidget::rowAt(int y) const {
    for (int i = 0; i < m_rows.size(); ++i)
        if (y >= m_rows[i].y && y < m_rows[i].y + m_rows[i].h) return i;
    return -1;
}

QRectF TimelineWidget::clipRectForId(const QString& id) {
    layoutRows();
    const ClipRef ref = m_project->timeline().find(id);
    if (!ref.valid()) return {};
    for (const Row& r : m_rows)
        if (r.type == ref.type && r.index == ref.track) return clipRect(*m_project->timeline().clip(ref), r);
    return {};
}

QRectF TimelineWidget::clipRect(const Clip& c, const Row& r) const {
    return QRectF(timeToX(c.start), r.y + 2, std::max(1.0, c.duration * m_pps), r.h - 4);
}

TimelineWidget::Hit TimelineWidget::hitTest(const QPoint& p) const {
    Hit h;
    if (p.x() < headerW() || p.y() < rulerH()) return h;
    const int ri = rowAt(p.y());
    if (ri < 0) return h;
    h.row = ri;
    const Row& r = m_rows[ri];
    const Track& tr = m_project->timeline().tracks(r.type)[r.index];
    for (const Clip& c : tr.clips) {
        const QRectF rc = clipRect(c, r);
        if (p.x() < rc.left() || p.x() > rc.right() || p.y() < rc.top() || p.y() > rc.bottom()) continue;
        h.clipId = c.id;
        const double edge = std::min(7.0, rc.width() / 3.0);
        if (p.x() - rc.left() <= edge) h.zone = Zone::LeftEdge;
        else if (rc.right() - p.x() <= edge) h.zone = Zone::RightEdge;
        else h.zone = Zone::Body;
        return h;
    }
    return h;
}

double TimelineWidget::quantize(double t) const { return std::max(0.0, snapToFrame(t, m_project->timeline().fps)); }

void TimelineWidget::updateScrollBars() {
    m_updatingBars = true;
    const double visible = std::max(1.0, contentRect().width() / m_pps);
    const double total = std::max(m_project->timeline().duration() + visible * 0.5, visible);
    m_hbar->setGeometry(headerW(), height() - scrollBarSize(), width() - headerW() - scrollBarSize(), scrollBarSize());
    m_hbar->setRange(0, int(std::ceil((total - visible) * 100)));
    m_hbar->setPageStep(int(visible * 100));
    m_hbar->setSingleStep(int(visible * 10));
    m_hbar->setValue(int(m_origin * 100));
    const int viewH = height() - rulerH() - scrollBarSize();
    m_vbar->setGeometry(width() - scrollBarSize(), rulerH(), scrollBarSize(), height() - rulerH() - scrollBarSize());
    m_vbar->setRange(0, std::max(0, contentHeight() - viewH));
    m_vbar->setPageStep(viewH);
    m_vbar->setValue(m_vScroll);
    m_hbar->setVisible(true);
    m_vbar->setVisible(m_vbar->maximum() > 0);
    m_updatingBars = false;
}

void TimelineWidget::resizeEvent(QResizeEvent*) {
    const int viewH = height() - rulerH() - scrollBarSize();
    m_vScroll = std::max(0, std::min(m_vScroll, std::max(0, contentHeight() - viewH)));
    layoutRows();
    updateScrollBars();
}

void TimelineWidget::onTimelineChanged() {
    layoutRows();
    updateScrollBars();
    update();
}

void TimelineWidget::setRecording(int audioTrack, double start, double end) {
    m_recTrack = audioTrack;
    m_recStart = start;
    m_recEnd = end;
    update();
}

void TimelineWidget::setTool(Tool t) {
    m_tool = t;
    updateCursor(m_mouse);
    update();
}

void TimelineWidget::setPlayhead(double t) {
    m_playhead = t;
    if (m_follow && m_drag == Drag::None) ensurePlayheadVisible();
    update();
}

void TimelineWidget::ensurePlayheadVisible() {
    const double visible = contentRect().width() / m_pps;
    if (m_playhead < m_origin - 1e-6) {
        m_origin = std::max(0.0, m_playhead - visible * 0.1);
        updateScrollBars();
    } else if (m_playhead > m_origin + visible * 0.97) {
        m_origin = std::max(0.0, m_playhead - visible * 0.08);
        updateScrollBars();
    }
}

void TimelineWidget::zoomAt(double factor, double anchor) {
    const double x = timeToX(anchor);
    m_pps = std::max(1.0, std::min(4000.0, m_pps * factor));
    m_origin = std::max(0.0, anchor - (x - headerW()) / m_pps);
    updateScrollBars();
    update();
}
void TimelineWidget::zoomIn() { zoomAt(1.5, m_playhead); }
void TimelineWidget::zoomOut() { zoomAt(1 / 1.5, m_playhead); }
void TimelineWidget::zoomToFit() {
    const double dur = std::max(5.0, m_project->timeline().duration());
    m_pps = std::max(1.0, (contentRect().width() - 30) / dur);
    m_origin = 0;
    updateScrollBars();
    update();
}

QSet<QString> TimelineWidget::withLinked(const QSet<QString>& ids) const {
    return m_linked ? ops::expandLinked(m_project->timeline(), ids) : ids;
}

// ------------------------------------------------------------------ painting
void TimelineWidget::paintEvent(QPaintEvent*) {
    layoutRows();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.fillRect(rect(), theme::bg1);

    p.save();
    p.setClipRect(contentRect());
    paintRows(p);
    paintOverlays(p);
    p.restore();

    paintHeaders(p);
    paintRuler(p);

    // playhead line
    const double px = timeToX(m_playhead);
    if (px >= headerW() - 1 && px <= width() - scrollBarSize()) {
        p.setClipRect(QRect(headerW(), 0, width() - headerW() - scrollBarSize(), height() - scrollBarSize()));
        p.setPen(QPen(theme::accentHi, 1.5));
        p.drawLine(QPointF(px, rulerH() - 2), QPointF(px, height() - scrollBarSize()));
        p.setPen(Qt::NoPen);
        p.setBrush(theme::accentHi);
        QPainterPath head;
        head.moveTo(px - 7, 6);
        head.lineTo(px + 7, 6);
        head.lineTo(px + 7, 15);
        head.lineTo(px, rulerH() - 3);
        head.lineTo(px - 7, 15);
        head.closeSubpath();
        p.drawPath(head);
    }
}

void TimelineWidget::paintRuler(QPainter& p) {
    const QRect rr = rulerRect();
    p.save();
    p.setClipRect(rr);
    p.fillRect(rr, theme::bg2);
    const Timeline& tl = m_project->timeline();

    // work area (in/out)
    if (tl.markIn >= 0 || tl.markOut >= 0) {
        const double a = tl.markIn >= 0 ? timeToX(tl.markIn) : rr.left();
        const double b = tl.markOut >= 0 ? timeToX(tl.markOut) : rr.right();
        p.fillRect(QRectF(a, rr.top() + 2, std::max(2.0, b - a), 6), QColor(145, 132, 217, 150));
    }

    // ticks
    const double fps = tl.fps;
    static const double secSteps[] = {1, 2, 5, 10, 15, 30, 60, 120, 300, 600, 900, 1800, 3600, 7200};
    QVector<double> cands = {1 / fps, 2 / fps, 5 / fps, 10 / fps, 15 / fps};
    for (double s : secSteps) cands << s;
    double step = cands.last();
    for (double c : cands)
        if (c * m_pps >= 110) { step = c; break; }
    const double t0 = std::max(0.0, xToTime(rr.left())), t1 = xToTime(rr.right());
    QFont f = p.font();
    f.setPixelSize(10);
    p.setFont(f);
    const int minorN = 5;
    const double minorPx = step * m_pps / minorN;
    for (double t = std::floor(t0 / step) * step; t <= t1 + step; t += step) {
        const double x = timeToX(t);
        p.setPen(theme::line.lighter(130));
        p.drawLine(QPointF(x, rr.bottom() - 13), QPointF(x, rr.bottom()));
        if (minorPx >= 7) {
            p.setPen(theme::line);
            for (int i = 1; i < minorN; ++i) {
                const double mx = x + i * step * m_pps / minorN;
                p.drawLine(QPointF(mx, rr.bottom() - 6), QPointF(mx, rr.bottom()));
            }
        }
        p.setPen(theme::textDim);
        p.drawText(QPointF(x + 4, rr.bottom() - 16 + 3), formatTimecode(std::max(0.0, t), fps));
    }
    p.setPen(theme::border);
    p.drawLine(rr.bottomLeft(), rr.bottomRight());

    // markers
    for (const Marker& m : tl.markers) {
        const double x = timeToX(m.time);
        QPainterPath d;
        d.moveTo(x, rr.bottom() - 12);
        d.lineTo(x + 5, rr.bottom() - 7);
        d.lineTo(x, rr.bottom() - 2);
        d.lineTo(x - 5, rr.bottom() - 7);
        d.closeSubpath();
        p.setPen(Qt::NoPen);
        p.setBrush(m.color);
        p.drawPath(d);
    }
    p.restore();

    // corner above the track headers
    p.fillRect(QRect(0, 0, headerW(), rulerH()), theme::bg2);
    p.setPen(theme::border);
    p.drawLine(0, rulerH() - 1, headerW(), rulerH() - 1);
    p.drawLine(headerW() - 1, 0, headerW() - 1, rulerH());
    p.setPen(theme::textDim);
    QFont cf = p.font();
    cf.setPixelSize(10);
    p.setFont(cf);
    p.drawText(QRect(10, 0, headerW() - 14, rulerH()), Qt::AlignVCenter | Qt::AlignLeft,
               QString("%1 × %2   %3 fps").arg(tl.width).arg(tl.height).arg(tl.fps, 0, 'f', tl.fps == std::floor(tl.fps) ? 0 : 2));
}

void TimelineWidget::paintRows(QPainter& p) {
    const Timeline& tl = m_project->timeline();
    const QSet<QString>& sel = m_project->selection();
    for (int i = 0; i < m_rows.size(); ++i) {
        const Row& r = m_rows[i];
        const Track& tr = tl.tracks(r.type)[r.index];
        const QRect row(headerW(), r.y, width() - headerW(), r.h);
        p.fillRect(row, (r.index % 2 == 0) ? theme::bg1 : theme::bg1.lighter(106));
        p.setPen(theme::border);
        p.drawLine(row.bottomLeft(), row.bottomRight());
        if (!tr.enabled) p.fillRect(row, QColor(0, 0, 0, 60));
        for (const Clip& c : tr.clips) {
            const QRectF rc = clipRect(c, r);
            if (rc.right() < headerW() || rc.left() > width()) continue;
            paintClip(p, c, r, sel.contains(c.id));
        }
        if (r.type == TrackType::Audio && r.index == m_recTrack) {
            const QRectF band(timeToX(m_recStart), r.y + 2, std::max(2.0, (m_recEnd - m_recStart) * m_pps), r.h - 4);
            p.setPen(QPen(QColor("#ff4d57"), 1.5));
            p.setBrush(QColor(255, 77, 87, 70));
            p.drawRoundedRect(band, 4, 4);
            p.setPen(QColor("#ff8a90"));
            QFont rf = p.font();
            rf.setPixelSize(11);
            rf.setBold(true);
            p.setFont(rf);
            p.drawText(band.adjusted(8, 0, -4, 0), Qt::AlignVCenter | Qt::AlignLeft, "● REC");
        }
    }
    if (tl.isEmpty() && !m_dropActive) {
        QFont f = p.font();
        f.setPixelSize(14);
        p.setFont(f);
        p.setPen(theme::textDim);
        const QRect area = contentRect();
        p.drawText(area, Qt::AlignCenter, "Drag media here to start your sequence");
    }
}

static QString elide(const QFontMetrics& fm, const QString& s, int w) { return fm.elidedText(s, Qt::ElideRight, std::max(0, w)); }

void TimelineWidget::paintClip(QPainter& p, const Clip& c, const Row& r, bool selected) {
    const Timeline& tl = m_project->timeline();
    const Track& tr = tl.tracks(r.type)[r.index];
    QRectF rc = clipRect(c, r);
    const bool audio = r.type == TrackType::Audio;
    QColor base = c.kind == ClipKind::Title ? theme::titleClip
                  : audio                   ? theme::audioClip
                  : c.mediaKind == MediaKind::Image ? theme::stillClip
                                                    : theme::videoClip;
    const bool dim = !c.enabled || !tr.enabled;
    if (dim) base = base.darker(220);

    QPainterPath path;
    path.addRoundedRect(rc, 4, 4);
    p.setPen(Qt::NoPen);
    p.setBrush(base.darker(selected ? 105 : 150));
    p.drawPath(path);

    p.save();
    p.setClipPath(path);
    QRectF head(rc.left(), rc.top(), rc.width(), kClipHead);
    p.fillRect(head, selected ? base.lighter(115) : base.darker(115));
    const QRectF body(rc.left(), rc.top() + kClipHead, rc.width(), rc.height() - kClipHead);

    const MediaAsset asset = c.mediaId.isEmpty() ? MediaAsset() : m_assets->get(c.mediaId);
    const double visL = std::max(body.left(), double(headerW()) - 40), visR = std::min(body.right(), double(width()));
    if (!audio && c.kind == ClipKind::Media && !asset.strip.isEmpty()) {
        const QImage& first = asset.strip.first();
        const double tileH = body.height();
        const double tileW = std::max(8.0, first.width() * tileH / std::max(1, first.height()));
        const int i0 = std::max(0, int(std::floor((visL - body.left()) / tileW)));
        for (int i = i0; body.left() + i * tileW < visR; ++i) {
            const double x = body.left() + i * tileW;
            int idx = 0;
            if (c.mediaKind != MediaKind::Image && asset.stripStep > 0) {
                const double st = c.srcIn + (i * tileW + tileW / 2) / m_pps * c.speed;
                idx = std::max(0, std::min(int(asset.strip.size()) - 1, int(st / asset.stripStep)));
            }
            p.drawImage(QRectF(x, body.top(), tileW, tileH), asset.strip[idx]);
        }
        p.fillRect(body, QColor(0, 0, 0, selected ? 20 : 55));
    } else if (c.kind == ClipKind::Title) {
        p.fillRect(body, base.darker(140));
        p.setPen(QColor(255, 255, 255, 190));
        QFont f = p.font();
        f.setPixelSize(13);
        f.setBold(true);
        p.setFont(f);
        const EffectInstance* tx = c.effect("text");
        p.drawText(body.adjusted(8, 0, -6, 0), Qt::AlignVCenter | Qt::AlignLeft,
                   elide(QFontMetrics(f), tx ? tx->text("text").replace('\n', ' ') : c.name, int(body.width() - 14)));
    } else if (audio) {
        p.fillRect(body, base.darker(185));
        if (asset.wavesReady && !asset.peaks100.isEmpty()) {
            const QVector<quint8>* pk = &asset.peaks100;
            double rate = 100;
            if (m_pps < 6) { pk = &asset.peaks1; rate = 1; }
            else if (m_pps < 60) { pk = &asset.peaks10; rate = 10; }
            const EffectInstance* vol = c.effect("volume");
            const double gain = vol && vol->enabled ? dbToGain(vol->num("level")) : 1.0;
            const double mid = body.center().y(), half = body.height() / 2 - 2;
            p.setPen(QPen(base.lighter(dim ? 110 : 160), 1.0));
            const int xa = int(std::max(visL, rc.left())), xb = int(std::min(visR, rc.right()));
            for (int x = xa; x <= xb; ++x) {
                const double ts = c.srcIn + (x - rc.left()) / m_pps * c.speed;
                const int i0 = std::max(0, int(ts * rate));
                const int i1 = std::max(i0, int((ts + c.speed / m_pps) * rate));
                int m = 0;
                for (int i = i0; i <= i1 && i < pk->size(); ++i) m = std::max<int>(m, (*pk)[i]);
                const double a = std::min(1.0, m / 255.0 * gain) * half;
                p.drawLine(QPointF(x, mid - std::max(0.5, a)), QPointF(x, mid + std::max(0.5, a)));
            }
        }
    } else {
        p.fillRect(body, base.darker(170));
    }

    // fades
    const EffectInstance* fx = c.effect(audio ? "volume" : "opacity");
    if (fx && fx->enabled) {
        p.setPen(QPen(QColor(255, 255, 255, 200), 1.2));
        const double fi = fx->num("fade_in") * m_pps, fo = fx->num("fade_out") * m_pps;
        if (fi > 2) p.drawLine(QPointF(rc.left(), rc.bottom()), QPointF(rc.left() + fi, body.top()));
        if (fo > 2) p.drawLine(QPointF(rc.right() - fo, body.top()), QPointF(rc.right(), rc.bottom()));
    }
    p.restore();

    // label
    p.save();
    p.setClipRect(rc.adjusted(2, 0, -2, 0));
    QFont lf = p.font();
    lf.setPixelSize(11);
    p.setFont(lf);
    const QFontMetrics fm(lf);
    double tx = rc.left() + 6;
    if (c.hasCustomEffects()) {
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, 110));
        p.drawRoundedRect(QRectF(tx - 2, rc.top() + 2, 16, 12), 3, 3);
        p.setPen(theme::accentHi);
        QFont fi = lf;
        fi.setItalic(true);
        fi.setBold(true);
        p.setFont(fi);
        p.drawText(QRectF(tx - 2, rc.top() + 2, 16, 12), Qt::AlignCenter, "fx");
        p.setFont(lf);
        tx += 18;
    }
    p.setPen(QColor(255, 255, 255, dim ? 150 : 235));
    p.drawText(QRectF(tx, rc.top(), rc.right() - tx - 4, kClipHead), Qt::AlignVCenter | Qt::AlignLeft,
               elide(fm, c.speed != 1.0 ? QString("%1  [%2%]").arg(c.name).arg(int(std::lround(c.speed * 100))) : c.name, int(rc.right() - tx - 6)));
    p.restore();

    // outline
    p.setBrush(Qt::NoBrush);
    p.setPen(selected ? QPen(Qt::white, 2.0) : QPen(QColor(0, 0, 0, 120), 1.0));
    p.drawPath(path);
}

void TimelineWidget::paintHeaders(QPainter& p) {
    const Timeline& tl = m_project->timeline();
    p.save();
    p.setClipRect(QRect(0, rulerH(), headerW(), height() - rulerH() - scrollBarSize()));
    p.fillRect(QRect(0, rulerH(), headerW(), height()), theme::bg2);
    QFont f = p.font();
    f.setPixelSize(11);
    f.setBold(true);
    p.setFont(f);
    for (const Row& r : m_rows) {
        const Track& tr = tl.tracks(r.type)[r.index];
        const QRect hr_(0, r.y, headerW(), r.h);
        p.fillRect(hr_, theme::bg2);
        p.setPen(theme::border);
        p.drawLine(hr_.bottomLeft(), hr_.bottomRight());
        p.drawLine(hr_.topRight(), hr_.bottomRight());

        const bool target = (r.type == TrackType::Video ? m_targetV : m_targetA) == r.index;
        const QRectF box(8, r.y + (r.h - 20) / 2.0, 30, 20);
        p.setPen(Qt::NoPen);
        p.setBrush(target ? theme::accent : theme::bg4);
        p.drawRoundedRect(box, 4, 4);
        p.setPen(target ? Qt::white : theme::textDim);
        p.drawText(box, Qt::AlignCenter, tr.name);

        auto icon = [&](icons::Name n, double x, bool on, const QColor& onColor) {
            const QPixmap pm = icons::pixmap(n, on ? onColor : theme::textDim.darker(125), 16);
            p.drawPixmap(QPointF(x, r.y + (r.h - 16) / 2.0), pm);
        };
        auto letter = [&](const QString& s, double x, bool on, const QColor& col) {
            const QRectF b(x, r.y + (r.h - 18) / 2.0, 20, 18);
            p.setPen(Qt::NoPen);
            p.setBrush(on ? col : theme::bg4);
            p.drawRoundedRect(b, 3, 3);
            p.setPen(on ? QColor("#101010") : theme::textDim);
            p.drawText(b, Qt::AlignCenter, s);
        };
        if (r.type == TrackType::Video) {
            icon(tr.enabled ? icons::Eye : icons::EyeOff, headerW() - 56, tr.enabled, theme::text);
        } else {
            icon(icons::Mic, 41, r.index == m_recTrack, QColor("#ff4d57"));
            letter("M", headerW() - 90, !tr.enabled, QColor("#e0a458"));
            letter("S", headerW() - 66, tr.solo, QColor("#e8d44d"));
        }
        icon(tr.locked ? icons::Lock : icons::Unlock, headerW() - 28, tr.locked, theme::danger);
    }
    p.restore();
}

void TimelineWidget::paintOverlays(QPainter& p) {
    const Timeline& tl = m_project->timeline();
    const int top = rulerH(), bottom = height() - scrollBarSize();
    // in/out area
    if (tl.markIn >= 0 || tl.markOut >= 0) {
        const double a = tl.markIn >= 0 ? timeToX(tl.markIn) : headerW();
        const double b = tl.markOut >= 0 ? timeToX(tl.markOut) : width();
        p.fillRect(QRectF(a, top, std::max(1.0, b - a), bottom - top), QColor(145, 132, 217, 22));
    }
    for (const Marker& m : tl.markers) {
        p.setPen(QPen(QColor(m.color.red(), m.color.green(), m.color.blue(), 90), 1, Qt::DashLine));
        p.drawLine(QPointF(timeToX(m.time), top), QPointF(timeToX(m.time), bottom));
    }
    if (m_snapLine >= 0) {
        p.setPen(QPen(QColor("#ffffff"), 1));
        p.drawLine(QPointF(timeToX(m_snapLine), top), QPointF(timeToX(m_snapLine), bottom));
    }
    if (m_tool == Tool::Razor && m_mouseIn && m_drag == Drag::None && m_mouse.x() >= headerW() && m_mouse.y() >= rulerH()) {
        p.setPen(QPen(QColor(255, 255, 255, 200), 1));
        p.drawLine(QPointF(m_mouse.x(), top), QPointF(m_mouse.x(), bottom));
    }
    if (m_drag == Drag::Marquee) {
        p.setPen(QPen(theme::accentHi, 1));
        p.setBrush(QColor(145, 132, 217, 50));
        p.drawRect(m_marquee);
    }
    if (m_dropActive && m_dropRow >= 0 && m_dropRow < m_rows.size()) {
        const Row& r = m_rows[m_dropRow];
        QColor col = r.type == TrackType::Video ? theme::videoClip : theme::audioClip;
        col.setAlpha(150);
        const QRectF g(timeToX(m_dropTime), r.y + 2, std::max(6.0, m_dropDur * m_pps), r.h - 4);
        p.setPen(QPen(Qt::white, 1.5, Qt::DashLine));
        p.setBrush(col);
        p.drawRoundedRect(g, 4, 4);
    }
}

// ------------------------------------------------------------------ editing helpers
double TimelineWidget::snapTime(double t, const QSet<QString>& exclude, bool* snapped, double extraEnd) {
    if (snapped) *snapped = false;
    if (!m_snap) return quantize(t);
    const double thr = 9.0 / m_pps;
    QVector<double> pts = ops::editPoints(m_project->timeline(), exclude);
    pts << m_playhead;
    double best = t, bestD = thr;
    for (double pt : pts) {
        const double d = std::abs(pt - t);
        if (d < bestD) { best = pt; bestD = d; if (snapped) *snapped = true; }
        if (extraEnd >= 0) {
            const double d2 = std::abs(pt - (t + extraEnd));
            if (d2 < bestD) { best = pt - extraEnd; bestD = d2; if (snapped) *snapped = true; }
        }
    }
    if (snapped && *snapped) { m_snapLine = (std::abs(best - t) < 1e-9 ? best : best); }
    return snapped && *snapped ? std::max(0.0, best) : quantize(t);
}

void TimelineWidget::selectClip(const QString& id, bool additive) {
    QSet<QString> set = withLinked({id});
    if (!additive) {
        m_project->setSelection(set);
        return;
    }
    QSet<QString> cur = m_project->selection();
    bool all = true;
    for (const QString& s : set) all = all && cur.contains(s);
    if (all) cur.subtract(set);
    else cur.unite(set);
    m_project->setSelection(cur);
}

// ------------------------------------------------------------------ mouse
void TimelineWidget::beginDrag(Drag d, const QPoint& p, const Hit& hit) {
    m_drag = d;
    m_pressPos = p;
    m_pressHit = hit;
    m_pressTime = xToTime(p.x());
    m_before = m_project->timeline();
    m_dragMoved = false;
    m_snapLine = -1;
    if (hit.row >= 0) {
        m_dragType = m_rows[hit.row].type;
        m_dragRow = m_rows[hit.row].index;
    }
}

bool TimelineWidget::headerClick(const QPoint& p) {
    const int ri = rowAt(p.y());
    if (ri < 0 || p.x() >= headerW()) return false;
    const Row& r = m_rows[ri];
    const Timeline& tl = m_project->timeline();
    const Track& tr = tl.tracks(r.type)[r.index];
    auto toggle = [&](const QString& text, std::function<void(Track&)> fn) {
        m_project->edit(text, [&](Timeline& t) { fn(t.tracks(r.type)[r.index]); });
    };
    if (p.x() >= headerW() - 34) {
        toggle(tr.locked ? "Unlock track" : "Lock track", [](Track& t) { t.locked = !t.locked; });
    } else if (r.type == TrackType::Video && p.x() >= headerW() - 62) {
        toggle("Toggle track visibility", [](Track& t) { t.enabled = !t.enabled; });
    } else if (r.type == TrackType::Audio && p.x() >= headerW() - 92 && p.x() < headerW() - 68) {
        toggle("Toggle mute", [](Track& t) { t.enabled = !t.enabled; });
    } else if (r.type == TrackType::Audio && p.x() >= headerW() - 68 && p.x() < headerW() - 44) {
        toggle("Toggle solo", [](Track& t) { t.solo = !t.solo; });
    } else if (r.type == TrackType::Audio && p.x() >= 38 && p.x() < headerW() - 92) {
        emit voiceOverRequested(r.index);          // the microphone button
    } else if (p.x() < 38) {
        (r.type == TrackType::Video ? m_targetV : m_targetA) = r.index;
        update();
    }
    return true;
}

void TimelineWidget::mousePressEvent(QMouseEvent* e) {
    layoutRows();
    setFocus();
    m_mouse = e->pos();
    if (e->button() == Qt::MiddleButton) {
        beginDrag(Drag::Pan, e->pos(), {});
        setCursor(Qt::ClosedHandCursor);
        return;
    }
    if (e->button() != Qt::LeftButton) return;
    const QPoint pos = e->pos();

    if (pos.x() < headerW()) {
        if (pos.y() >= rulerH()) headerClick(pos);
        return;
    }
    if (pos.y() < rulerH()) {
        // marker?
        const Timeline& tl = m_project->timeline();
        for (int i = 0; i < tl.markers.size(); ++i)
            if (std::abs(timeToX(tl.markers[i].time) - pos.x()) < 7 && pos.y() > rulerH() - 16) {
                emit seekRequested(tl.markers[i].time);
                return;
            }
        beginDrag(Drag::Scrub, pos, {});
        updateDrag(pos, e->modifiers());
        return;
    }

    const Hit hit = hitTest(pos);
    const double t = xToTime(pos.x());
    switch (m_tool) {
    case Tool::Hand:
        beginDrag(Drag::Pan, pos, {});
        setCursor(Qt::ClosedHandCursor);
        return;
    case Tool::Zoom:
        zoomAt((e->modifiers() & Qt::AltModifier) ? 1 / 2.0 : 2.0, t);
        return;
    case Tool::Razor:
        razorAt(pos, e->modifiers() & Qt::ShiftModifier);
        return;
    case Tool::Type: {
        const int ri = rowAt(pos.y());
        if (ri >= 0 && m_rows[ri].type == TrackType::Video) emit titleRequested(quantize(t), m_rows[ri].index);
        return;
    }
    default: break;
    }

    // selection / track-select-forward / ripple
    if (hit.zone != Zone::None) {
        const bool alt = e->modifiers() & Qt::AltModifier;
        const bool wasSelected = m_project->selection().contains(hit.clipId);
        if (e->modifiers() & Qt::ShiftModifier) {
            const bool old = m_linked;
            if (alt) m_linked = false;
            selectClip(hit.clipId, true);
            m_linked = old;
        } else if (m_tool == Tool::TrackForward) {
            QSet<QString> ids;
            const Clip* c0 = m_project->timeline().clip(hit.clipId);
            for (const auto* tracks : {&m_project->timeline().video, &m_project->timeline().audio})
                for (const Track& tr : *tracks)
                    for (const Clip& c : tr.clips)
                        if (c0 && c.start >= c0->start - 1e-6) ids.insert(c.id);
            m_project->setSelection(ids);
        } else if (!wasSelected) {
            const bool old = m_linked;
            if (alt) m_linked = false;
            selectClip(hit.clipId, false);
            m_linked = old;
        }
        if (!m_project->selection().contains(hit.clipId)) return;

        QSet<QString> ids;
        Drag mode = Drag::Move;
        if (hit.zone == Zone::LeftEdge || hit.zone == Zone::RightEdge) {
            mode = hit.zone == Zone::LeftEdge ? Drag::TrimLeft : Drag::TrimRight;
            const bool old = m_linked;
            if (alt) m_linked = false;
            ids = withLinked({hit.clipId});
            m_linked = old;
        } else {
            ids = m_project->selection();
        }
        beginDrag(mode, pos, hit);
        m_dragIds = ids;
        return;
    }

    // empty space: deselect + marquee
    m_marqueeBase = (e->modifiers() & Qt::ShiftModifier) ? m_project->selection() : QSet<QString>();
    if (!(e->modifiers() & Qt::ShiftModifier)) m_project->setSelection({});
    beginDrag(Drag::Marquee, pos, hit);
    m_marquee = QRect(pos, QSize());
}

void TimelineWidget::razorAt(const QPoint& p, bool allTracks) {
    const Hit hit = hitTest(p);
    const double t = snapTime(xToTime(p.x()), {});
    m_snapLine = -1;
    Timeline probe = m_project->timeline();
    if (allTracks) {
        if (ops::splitAllAt(probe, t) > 0)
            m_project->edit("Add edit to all tracks", [&](Timeline& tl) { ops::splitAllAt(tl, t); });
    } else if (!hit.clipId.isEmpty()) {
        if (ops::splitAt(probe, hit.clipId, t))
            m_project->edit("Split clip", [&](Timeline& tl) { ops::splitAt(tl, hit.clipId, t); });
    }
}

void TimelineWidget::updateDrag(const QPoint& p, Qt::KeyboardModifiers mods) {
    const Timeline& base = m_before;
    const double fps = base.fps;
    switch (m_drag) {
    case Drag::Scrub: {
        const double t = std::max(0.0, xToTime(p.x()));
        bool snapped = false;
        const double st = (mods & Qt::ShiftModifier) ? snapTime(t, {}, &snapped) : quantize(t);
        m_snapLine = snapped ? st : -1;
        emit seekRequested(st);
        update();
        break;
    }
    case Drag::Move: {
        const int dx = p.x() - m_pressPos.x(), dy = p.y() - m_pressPos.y();
        if (!m_dragMoved && std::abs(dx) < 3 && std::abs(dy) < 3) return;
        m_dragMoved = true;
        const Clip* primary = base.clip(m_pressHit.clipId);
        if (!primary) return;
        double dt = dx / m_pps;
        bool snapped = false;
        m_snapLine = -1;
        if (m_snap && !(mods & Qt::ShiftModifier)) {
            const double thr = 9.0 / m_pps;
            QVector<double> pts = ops::editPoints(base, m_dragIds);
            pts << m_playhead;
            double bestD = thr, bestDt = dt;
            for (double pt : pts) {
                const double a = pt - primary->start;                 // align start with pt
                const double b = pt - primary->end();                 // align end with pt
                if (std::abs(a - dt) < bestD) { bestD = std::abs(a - dt); bestDt = a; snapped = true; m_snapLine = pt; }
                if (std::abs(b - dt) < bestD) { bestD = std::abs(b - dt); bestDt = b; snapped = true; m_snapLine = pt; }
            }
            dt = bestDt;
        }
        if (!snapped) dt = std::round(dt * fps) / fps;
        int dTrack = 0;
        const int ri = rowAt(p.y());
        if (ri >= 0 && m_rows[ri].type == m_dragType) dTrack = m_rows[ri].index - m_dragRow;
        else if (ri >= 0) dTrack = 0;
        m_project->setTimelineLive(ops::moved(base, m_dragIds, dt, dTrack, m_dragType));
        emit statusMessage(QString("Move  %1%2").arg(dt >= 0 ? "+" : "-").arg(formatTimecode(std::abs(dt), fps)));
        break;
    }
    case Drag::TrimLeft:
    case Drag::TrimRight: {
        const int dx = p.x() - m_pressPos.x();
        if (!m_dragMoved && std::abs(dx) < 2) return;
        m_dragMoved = true;
        const bool left = m_drag == Drag::TrimLeft;
        const Clip* primary = base.clip(m_pressHit.clipId);
        if (!primary) return;
        double delta = dx / m_pps;
        const double edge = left ? primary->start : primary->end();
        bool snapped = false;
        m_snapLine = -1;
        if (m_snap && !(mods & Qt::ShiftModifier)) {
            const double thr = 9.0 / m_pps;
            QVector<double> pts = ops::editPoints(base, m_dragIds);
            pts << m_playhead;
            double bestD = thr;
            for (double pt : pts)
                if (std::abs(pt - (edge + delta)) < bestD) { bestD = std::abs(pt - (edge + delta)); delta = pt - edge; snapped = true; m_snapLine = pt; }
        }
        if (!snapped) delta = std::round(delta * fps) / fps;
        Timeline t = base;
        ops::trim(t, m_dragIds, left, delta, m_tool == Tool::Ripple);
        m_project->setTimelineLive(t);
        const Clip* now = t.clip(m_pressHit.clipId);
        if (now) emit statusMessage(QString("Duration  %1").arg(formatTimecode(now->duration, fps)));
        break;
    }
    case Drag::Marquee: {
        m_marquee = QRect(m_pressPos, p).normalized();
        QSet<QString> hitSet = m_marqueeBase;
        const Timeline& tl = m_project->timeline();
        for (const Row& r : m_rows) {
            for (const Clip& c : tl.tracks(r.type)[r.index].clips)
                if (clipRect(c, r).intersects(QRectF(m_marquee))) hitSet.insert(c.id);
        }
        m_project->setSelection(withLinked(hitSet));
        update();
        break;
    }
    case Drag::Pan: {
        m_origin = std::max(0.0, m_origin - (p.x() - m_pressPos.x()) / m_pps);
        m_vScroll = std::max(0, m_vScroll - (p.y() - m_pressPos.y()));
        m_pressPos = p;
        layoutRows();
        updateScrollBars();
        update();
        break;
    }
    default: break;
    }
}

void TimelineWidget::endDrag() {
    const Drag d = m_drag;
    m_drag = Drag::None;
    m_snapLine = -1;
    if ((d == Drag::Move || d == Drag::TrimLeft || d == Drag::TrimRight) && m_dragMoved) {
        const QString text = d == Drag::Move ? "Move clips" : (m_tool == Tool::Ripple ? "Ripple trim" : "Trim clip");
        m_project->commit(text, m_before);
    }
    emit statusMessage(QString());
    updateCursor(m_mouse);
    update();
}

void TimelineWidget::mouseMoveEvent(QMouseEvent* e) {
    layoutRows();
    m_mouse = e->pos();
    m_mouseIn = true;
    if (m_drag != Drag::None) {
        updateDrag(e->pos(), e->modifiers());
        return;
    }
    updateCursor(e->pos());
    if (m_tool == Tool::Razor) update();
}

void TimelineWidget::mouseReleaseEvent(QMouseEvent* e) {
    if (m_drag != Drag::None && (e->button() == Qt::LeftButton || e->button() == Qt::MiddleButton)) endDrag();
}

void TimelineWidget::mouseDoubleClickEvent(QMouseEvent* e) {
    // double-click on a clip selects the clip together with everything linked to it
    if (e->button() != Qt::LeftButton) return;
    const Hit hit = hitTest(e->pos());
    if (!hit.clipId.isEmpty() && m_tool == Tool::Selection) selectClip(hit.clipId, false);
}

void TimelineWidget::leaveEvent(QEvent*) {
    m_mouseIn = false;
    update();
}

void TimelineWidget::keyPressEvent(QKeyEvent* e) {
    if (e->key() == Qt::Key_Escape && m_drag != Drag::None) {
        m_drag = Drag::None;
        m_project->setTimelineLive(m_before);
        m_snapLine = -1;
        update();
        return;
    }
    QWidget::keyPressEvent(e);
}

void TimelineWidget::updateCursor(const QPoint& p) {
    Qt::CursorShape c = Qt::ArrowCursor;
    switch (m_tool) {
    case Tool::Razor: c = Qt::CrossCursor; break;
    case Tool::Hand: c = Qt::OpenHandCursor; break;
    case Tool::Zoom: c = Qt::PointingHandCursor; break;
    case Tool::Type: c = Qt::IBeamCursor; break;
    default: {
        const Hit h = hitTest(p);
        if (h.zone == Zone::LeftEdge || h.zone == Zone::RightEdge) c = Qt::SizeHorCursor;
        else if (h.zone == Zone::Body) c = Qt::ArrowCursor;
        break;
    }
    }
    if (p.y() < rulerH() && p.x() >= headerW()) c = Qt::ArrowCursor;
    setCursor(c);
}

void TimelineWidget::wheelEvent(QWheelEvent* e) {
    const QPoint d = e->angleDelta();
    if (e->modifiers() & (Qt::ControlModifier | Qt::AltModifier)) {
        const double f = std::pow(1.0015, d.y() ? d.y() : d.x());
        zoomAt(f, xToTime(e->position().x()));
    } else if ((e->modifiers() & Qt::ShiftModifier) || std::abs(d.x()) > std::abs(d.y()) ||
               m_vbar->maximum() == 0) {
        const double dx = (d.x() ? d.x() : d.y());
        m_origin = std::max(0.0, m_origin - dx / 120.0 * (contentRect().width() / m_pps) * 0.12);
        updateScrollBars();
    } else {
        m_vScroll = std::max(0, std::min(m_vbar->maximum(), m_vScroll - d.y() / 2));
        layoutRows();
        updateScrollBars();
    }
    update();
    e->accept();
}

// ------------------------------------------------------------------ drag & drop
double TimelineWidget::durationForDrop(const QStringList& ids) const {
    if (ids.isEmpty()) return 5.0;
    if (const MediaItem* m = m_project->mediaById(ids.first())) {
        const double len = (m->markIn >= 0 || m->markOut >= 0)
                               ? (m->markOut >= 0 ? m->markOut : m->duration) - std::max(0.0, m->markIn)
                               : m->duration;
        return std::max(0.1, len);
    }
    return 5.0;
}

void TimelineWidget::dragEnterEvent(QDragEnterEvent* e) {
    const QMimeData* m = e->mimeData();
    if (m->hasFormat(kMediaMime) || m->hasFormat(kEffectMime) || m->hasUrls()) e->acceptProposedAction();
}

void TimelineWidget::dragMoveEvent(QDragMoveEvent* e) {
    layoutRows();
    const QMimeData* m = e->mimeData();
    if (m->hasFormat(kMediaMime) || m->hasUrls()) {
        QStringList ids;
        if (m->hasFormat(kMediaMime)) ids = QString::fromUtf8(m->data(kMediaMime)).split('\n', Qt::SkipEmptyParts);
        m_dropDur = m->hasFormat(kMediaMime) ? durationForDrop(ids) : 5.0;
        m_dropTime = snapTime(std::max(0.0, xToTime(e->position().x())), {}, nullptr, m_dropDur);
        m_dropRow = rowAt(int(e->position().y()));
        m_dropActive = m_dropRow >= 0 && e->position().x() >= headerW();
        update();
        if (m_dropActive) e->acceptProposedAction();
        else e->ignore();
    } else if (m->hasFormat(kEffectMime)) {
        e->acceptProposedAction();
    }
}

void TimelineWidget::dragLeaveEvent(QDragLeaveEvent*) {
    m_dropActive = false;
    m_snapLine = -1;
    update();
}

void TimelineWidget::dropEvent(QDropEvent* e) {
    layoutRows();
    const QMimeData* m = e->mimeData();
    m_dropActive = false;
    m_snapLine = -1;
    const int ri = rowAt(int(e->position().y()));
    if (m->hasFormat(kEffectMime)) {
        const Hit hit = hitTest(e->position().toPoint());
        if (!hit.clipId.isEmpty()) emit effectDropped(hit.clipId, QString::fromUtf8(m->data(kEffectMime)));
        e->acceptProposedAction();
        update();
        return;
    }
    if (ri < 0 || e->position().x() < headerW()) { update(); return; }
    int v = m_targetV, a = m_targetA;
    if (m_rows[ri].type == TrackType::Video) v = m_rows[ri].index;
    else a = m_rows[ri].index;
    if (m->hasFormat(kMediaMime)) {
        const QStringList ids = QString::fromUtf8(m->data(kMediaMime)).split('\n', Qt::SkipEmptyParts);
        emit mediaDropped(ids, m_dropTime, v, a);
        e->acceptProposedAction();
    } else if (m->hasUrls()) {
        QStringList paths;
        for (const QUrl& u : m->urls())
            if (u.isLocalFile()) paths << u.toLocalFile();
        if (!paths.isEmpty()) emit filesDropped(paths, std::max(0.0, quantize(xToTime(e->position().x()))), v, a);
        e->acceptProposedAction();
    }
    update();
}

void TimelineWidget::contextMenuEvent(QContextMenuEvent* e) {
    layoutRows();
    if (e->pos().y() < rulerH() && e->pos().x() >= headerW()) {
        const Timeline& tl = m_project->timeline();
        for (int i = 0; i < tl.markers.size(); ++i)
            if (std::abs(timeToX(tl.markers[i].time) - e->pos().x()) < 8) {
                QMenu menu(this);
                menu.addAction("Delete Marker", [this, i] {
                    m_project->edit("Delete marker", [i](Timeline& t) { if (i < t.markers.size()) t.markers.removeAt(i); });
                });
                menu.exec(e->globalPos());
                return;
            }
        return;
    }
    const Hit hit = hitTest(e->pos());
    if (hit.clipId.isEmpty()) return;
    if (!m_project->selection().contains(hit.clipId)) selectClip(hit.clipId, false);
    emit contextMenuRequested(e->globalPos(), hit.clipId);
}

}  // namespace hr
