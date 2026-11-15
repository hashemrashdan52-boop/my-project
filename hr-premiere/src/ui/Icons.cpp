#include "Icons.h"

#include "Theme.h"

#include <QPainter>
#include <QPainterPath>
#include <QSvgRenderer>
#include <QtMath>

namespace hr::icons {

// Every glyph is drawn on a 24x24 grid.
static void draw(QPainter& p, Name n, const QColor& c) {
    QPen pen(c, 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    auto fill = [&] { p.setPen(Qt::NoPen); p.setBrush(c); };
    auto stroke = [&] { p.setPen(pen); p.setBrush(Qt::NoBrush); };
    auto poly = [&](std::initializer_list<QPointF> pts) { p.drawPolygon(QPolygonF(QVector<QPointF>(pts))); };

    switch (n) {
    case Play: fill(); poly({{7, 4.5}, {19, 12}, {7, 19.5}}); break;
    case Pause: fill(); p.drawRoundedRect(QRectF(6, 5, 4, 14), 1, 1); p.drawRoundedRect(QRectF(14, 5, 4, 14), 1, 1); break;
    case StepBack: fill(); p.drawRoundedRect(QRectF(5, 5, 2.5, 14), 1, 1); poly({{19, 5}, {19, 19}, {9, 12}}); break;
    case StepForward: fill(); p.drawRoundedRect(QRectF(16.5, 5, 2.5, 14), 1, 1); poly({{5, 5}, {5, 19}, {15, 12}}); break;
    case ToStart: fill(); p.drawRoundedRect(QRectF(4, 5, 2.5, 14), 1, 1); poly({{12, 12}, {20, 5}, {20, 19}}); break;
    case ToEnd: fill(); p.drawRoundedRect(QRectF(17.5, 5, 2.5, 14), 1, 1); poly({{12, 12}, {4, 5}, {4, 19}}); break;
    case MarkIn: stroke(); { QPainterPath q; q.moveTo(15, 4); q.lineTo(9, 4); q.lineTo(9, 20); q.lineTo(15, 20); p.drawPath(q); } p.drawLine(QPointF(13, 12), QPointF(19, 12)); break;
    case MarkOut: stroke(); { QPainterPath q; q.moveTo(9, 4); q.lineTo(15, 4); q.lineTo(15, 20); q.lineTo(9, 20); p.drawPath(q); } p.drawLine(QPointF(5, 12), QPointF(11, 12)); break;
    case Insert: stroke(); p.drawLine(QPointF(4, 20), QPointF(20, 20)); p.drawLine(QPointF(12, 4), QPointF(12, 15)); p.drawLine(QPointF(7.5, 11), QPointF(12, 15.5)); p.drawLine(QPointF(16.5, 11), QPointF(12, 15.5)); break;
    case Overwrite: stroke(); p.drawRoundedRect(QRectF(4, 14, 16, 6), 1.5, 1.5); p.drawLine(QPointF(12, 3.5), QPointF(12, 11)); p.drawLine(QPointF(8, 8), QPointF(12, 12)); p.drawLine(QPointF(16, 8), QPointF(12, 12)); break;
    case Camera: stroke(); p.drawRoundedRect(QRectF(3.5, 7, 17, 12), 2, 2); p.drawEllipse(QPointF(12, 13), 3.4, 3.4); p.drawLine(QPointF(8, 7), QPointF(9.5, 4.5)); p.drawLine(QPointF(9.5, 4.5), QPointF(14.5, 4.5)); p.drawLine(QPointF(14.5, 4.5), QPointF(16, 7)); break;
    case Selection: fill(); poly({{6, 3.5}, {6, 18.5}, {10, 14.8}, {12.7, 20.5}, {15.2, 19.4}, {12.5, 13.8}, {17.8, 13.5}}); break;
    case TrackForward: fill(); poly({{4, 5}, {4, 17}, {7.4, 14}, {9.6, 18.5}, {11.6, 17.6}, {9.4, 13.2}, {13.5, 13}}); stroke(); p.drawLine(QPointF(15, 6), QPointF(19, 10)); p.drawLine(QPointF(19, 10), QPointF(15, 14)); p.drawLine(QPointF(15, 16), QPointF(19, 20)); p.drawLine(QPointF(19, 20), QPointF(15, 24)); break;
    case Ripple: stroke(); p.drawLine(QPointF(12, 4), QPointF(12, 20)); p.drawLine(QPointF(12, 12), QPointF(4, 12)); p.drawLine(QPointF(7, 9), QPointF(4, 12)); p.drawLine(QPointF(7, 15), QPointF(4, 12)); p.drawLine(QPointF(12, 12), QPointF(20, 12)); p.drawLine(QPointF(17, 9), QPointF(20, 12)); p.drawLine(QPointF(17, 15), QPointF(20, 12)); break;
    case Razor: stroke(); p.drawLine(QPointF(12, 3), QPointF(12, 21)); p.setPen(Qt::NoPen); p.setBrush(c); poly({{5, 6}, {10, 12}, {5, 18}}); poly({{19, 6}, {14, 12}, {19, 18}}); break;
    case Hand: stroke(); { QPainterPath q; q.moveTo(7, 12); q.lineTo(7, 7.5); q.moveTo(10.2, 11); q.lineTo(10.2, 5); q.moveTo(13.4, 11); q.lineTo(13.4, 5.5); q.moveTo(16.6, 12); q.lineTo(16.6, 8); q.moveTo(7, 12); q.lineTo(7, 13.5); q.cubicTo(7, 19, 9, 20.5, 12, 20.5); q.cubicTo(15, 20.5, 16.6, 18, 16.6, 14); q.lineTo(16.6, 12); p.drawPath(q); } break;
    case Zoom: stroke(); p.drawEllipse(QPointF(10.5, 10.5), 6, 6); p.drawLine(QPointF(15, 15), QPointF(20, 20)); p.drawLine(QPointF(8, 10.5), QPointF(13, 10.5)); p.drawLine(QPointF(10.5, 8), QPointF(10.5, 13)); break;
    case Type: stroke(); p.drawLine(QPointF(6, 6), QPointF(18, 6)); p.drawLine(QPointF(12, 6), QPointF(12, 19)); p.drawLine(QPointF(9.5, 19), QPointF(14.5, 19)); break;
    case Magnet: stroke(); { QPainterPath q; q.moveTo(6, 4); q.lineTo(6, 12); q.cubicTo(6, 19, 18, 19, 18, 12); q.lineTo(18, 4); p.drawPath(q); } p.drawLine(QPointF(6, 8), QPointF(10, 8)); p.drawLine(QPointF(14, 8), QPointF(18, 8)); break;
    case Link: stroke(); p.drawRoundedRect(QRectF(3.5, 8.5, 9, 7), 3.5, 3.5); p.drawRoundedRect(QRectF(11.5, 8.5, 9, 7), 3.5, 3.5); break;
    case Marker: fill(); poly({{12, 3.5}, {19, 12}, {12, 20.5}, {5, 12}}); break;
    case Wrench: stroke(); { QPainterPath q; q.moveTo(20, 8); q.cubicTo(20, 5, 17, 3.5, 14.5, 4.5); q.lineTo(17, 7.5); q.lineTo(15.5, 9); q.lineTo(12.5, 6.5); q.cubicTo(11.5, 9, 13, 12, 16, 12); q.lineTo(7, 21); q.lineTo(4, 18); q.lineTo(13, 9); p.drawPath(q); } break;
    case Eye: stroke(); { QPainterPath q; q.moveTo(2.5, 12); q.cubicTo(6, 5.5, 18, 5.5, 21.5, 12); q.cubicTo(18, 18.5, 6, 18.5, 2.5, 12); p.drawPath(q); } fill(); p.drawEllipse(QPointF(12, 12), 3, 3); break;
    case EyeOff: stroke(); { QPainterPath q; q.moveTo(2.5, 12); q.cubicTo(6, 5.5, 18, 5.5, 21.5, 12); q.cubicTo(18, 18.5, 6, 18.5, 2.5, 12); p.drawPath(q); } p.drawLine(QPointF(4, 20), QPointF(20, 4)); break;
    case Lock: stroke(); p.drawRoundedRect(QRectF(5.5, 11, 13, 9.5), 2, 2); { QPainterPath q; q.moveTo(8.5, 11); q.lineTo(8.5, 8); q.cubicTo(8.5, 3.5, 15.5, 3.5, 15.5, 8); q.lineTo(15.5, 11); p.drawPath(q); } break;
    case Unlock: stroke(); p.drawRoundedRect(QRectF(5.5, 11, 13, 9.5), 2, 2); { QPainterPath q; q.moveTo(8.5, 11); q.lineTo(8.5, 8); q.cubicTo(8.5, 3.5, 15.5, 3.5, 15.5, 6); p.drawPath(q); } break;
    case Speaker: fill(); poly({{4, 9.5}, {8, 9.5}, {13, 5}, {13, 19}, {8, 14.5}, {4, 14.5}}); stroke(); { QPainterPath q; q.moveTo(16, 9); q.cubicTo(18, 11, 18, 13, 16, 15); p.drawPath(q); } break;
    case SpeakerOff: fill(); poly({{4, 9.5}, {8, 9.5}, {13, 5}, {13, 19}, {8, 14.5}, {4, 14.5}}); stroke(); p.drawLine(QPointF(16, 9), QPointF(21, 15)); p.drawLine(QPointF(21, 9), QPointF(16, 15)); break;
    case Folder: stroke(); { QPainterPath q; q.moveTo(3.5, 7); q.lineTo(3.5, 18.5); q.lineTo(20.5, 18.5); q.lineTo(20.5, 8.5); q.lineTo(11.5, 8.5); q.lineTo(9.5, 5.5); q.lineTo(3.5, 5.5); q.closeSubpath(); p.drawPath(q); } break;
    case ListView: stroke(); for (int i = 0; i < 3; ++i) { p.drawLine(QPointF(9, 6.5 + i * 5.5), QPointF(20, 6.5 + i * 5.5)); p.drawPoint(QPointF(4.8, 6.5 + i * 5.5)); } break;
    case IconView: stroke(); p.drawRoundedRect(QRectF(4, 4, 6.5, 6.5), 1.2, 1.2); p.drawRoundedRect(QRectF(13.5, 4, 6.5, 6.5), 1.2, 1.2); p.drawRoundedRect(QRectF(4, 13.5, 6.5, 6.5), 1.2, 1.2); p.drawRoundedRect(QRectF(13.5, 13.5, 6.5, 6.5), 1.2, 1.2); break;
    case Search: stroke(); p.drawEllipse(QPointF(10.5, 10.5), 6, 6); p.drawLine(QPointF(15, 15), QPointF(20, 20)); break;
    case Trash: stroke(); p.drawLine(QPointF(4, 7), QPointF(20, 7)); p.drawLine(QPointF(9.5, 7), QPointF(9.5, 4.5)); p.drawLine(QPointF(9.5, 4.5), QPointF(14.5, 4.5)); p.drawLine(QPointF(14.5, 4.5), QPointF(14.5, 7)); { QPainterPath q; q.moveTo(6, 7); q.lineTo(7, 20); q.lineTo(17, 20); q.lineTo(18, 7); p.drawPath(q); } break;
    case Plus: stroke(); p.drawLine(QPointF(12, 5), QPointF(12, 19)); p.drawLine(QPointF(5, 12), QPointF(19, 12)); break;
    case Film: stroke(); p.drawRoundedRect(QRectF(4, 4, 16, 16), 2, 2); p.drawLine(QPointF(8, 4), QPointF(8, 20)); p.drawLine(QPointF(16, 4), QPointF(16, 20)); p.drawLine(QPointF(4, 9), QPointF(8, 9)); p.drawLine(QPointF(4, 15), QPointF(8, 15)); p.drawLine(QPointF(16, 9), QPointF(20, 9)); p.drawLine(QPointF(16, 15), QPointF(20, 15)); break;
    case Music: stroke(); p.drawLine(QPointF(9, 17), QPointF(9, 5)); p.drawLine(QPointF(9, 5), QPointF(19, 3.5)); p.drawLine(QPointF(19, 3.5), QPointF(19, 15)); p.drawEllipse(QPointF(6.8, 17.2), 2.6, 2.1); p.drawEllipse(QPointF(16.8, 15.2), 2.6, 2.1); break;
    case ImageFile: stroke(); p.drawRoundedRect(QRectF(3.5, 5, 17, 14), 2, 2); fill(); poly({{6, 17}, {10.5, 11}, {14, 15}, {16, 13}, {19, 17}}); p.drawEllipse(QPointF(16, 9), 1.6, 1.6); break;
    case Title: stroke(); p.drawRoundedRect(QRectF(3.5, 5, 17, 14), 2, 2); p.drawLine(QPointF(8, 9.5), QPointF(16, 9.5)); p.drawLine(QPointF(12, 9.5), QPointF(12, 15.5)); break;
    case Fx: stroke(); { QPainterPath q; q.moveTo(4, 6); q.lineTo(10, 6); q.moveTo(4, 12); q.lineTo(9, 12); q.moveTo(7, 6); q.lineTo(7, 18); p.drawPath(q); } p.drawLine(QPointF(12, 10), QPointF(19, 19)); p.drawLine(QPointF(19, 10), QPointF(12, 19)); break;
    case Import: stroke(); p.drawLine(QPointF(12, 4), QPointF(12, 15)); p.drawLine(QPointF(7.5, 11), QPointF(12, 15.5)); p.drawLine(QPointF(16.5, 11), QPointF(12, 15.5)); { QPainterPath q; q.moveTo(4, 15); q.lineTo(4, 20); q.lineTo(20, 20); q.lineTo(20, 15); p.drawPath(q); } break;
    case Mic: stroke(); p.drawRoundedRect(QRectF(9, 3, 6, 11), 3, 3); { QPainterPath q; q.moveTo(5.5, 11); q.cubicTo(5.5, 17.5, 18.5, 17.5, 18.5, 11); p.drawPath(q); } p.drawLine(QPointF(12, 17.5), QPointF(12, 21)); p.drawLine(QPointF(8.5, 21), QPointF(15.5, 21)); break;
    case Check: stroke(); { QPainterPath q; q.moveTo(5, 12.5); q.lineTo(10, 17.5); q.lineTo(19, 7); p.drawPath(q); } break;
    }
}

QPixmap pixmap(Name n, const QColor& color, int size) {
    const qreal dpr = 2.0;
    QPixmap pm(int(size * dpr), int(size * dpr));
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.scale(size / 24.0, size / 24.0);
    draw(p, n, color);
    return pm;
}

QIcon make(Name n, const QColor& color, const QColor& active) {
    QIcon ic;
    const QColor on = active.isValid() ? active : color;
    for (int s : {16, 20, 24, 32}) {
        ic.addPixmap(pixmap(n, color, s), QIcon::Normal, QIcon::Off);
        ic.addPixmap(pixmap(n, on, s), QIcon::Normal, QIcon::On);
        ic.addPixmap(pixmap(n, on, s), QIcon::Active, QIcon::Off);
        ic.addPixmap(pixmap(n, theme::textDim.darker(130), s), QIcon::Disabled, QIcon::Off);
    }
    return ic;
}

QIcon make(Name n) { return make(n, theme::text, QColor("#ffffff")); }

QPixmap logo(int size) {
    static QSvgRenderer renderer(QStringLiteral(":/logo.svg"));
    const qreal dpr = 2.0;
    QPixmap pm(int(size * dpr), int(size * dpr));
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    renderer.render(&p, QRectF(0, 0, size, size));
    return pm;
}

}  // namespace hr::icons
