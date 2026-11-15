#include "AudioMeters.h"

#include "Theme.h"

#include <QPainter>
#include <cmath>

namespace hr {

AudioMeters::AudioMeters(QWidget* parent) : QWidget(parent) { setFixedWidth(64); }

void AudioMeters::setLevels(float l, float r) {
    m_l = l;
    m_r = r;
    m_holdL = std::max(l, m_holdL * 0.985f);
    m_holdR = std::max(r, m_holdR * 0.985f);
    update();
}

static double dbPos(float amp) {  // 0..1 along the meter, -60 dB .. 0 dB
    if (amp <= 0.000001f) return 0;
    const double db = 20.0 * std::log10(double(amp));
    return std::max(0.0, std::min(1.0, (db + 60.0) / 60.0));
}

void AudioMeters::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setLayoutDirection(Qt::LeftToRight);   // painted LTR also in the Arabic version
    p.setRenderHint(QPainter::Antialiasing, true);
    p.fillRect(rect(), theme::bg1);
    const int top = 36, bottom = height() - 10;
    const int h = std::max(10, bottom - top);
    const int barW = 10;
    const int x1 = 30, x2 = x1 + barW + 4;

    QLinearGradient g(0, bottom, 0, top);
    g.setColorAt(0.0, QColor("#2fae6d"));
    g.setColorAt(0.70, QColor("#37c47a"));
    g.setColorAt(0.84, QColor("#e6d34a"));
    g.setColorAt(0.93, QColor("#f08a3c"));
    g.setColorAt(1.0, QColor("#e0454c"));

    auto bar = [&](int x, float level, float hold) {
        p.fillRect(QRect(x, top, barW, h), theme::bg3);
        const double lv = dbPos(level);
        p.setBrush(g);
        p.setPen(Qt::NoPen);
        p.drawRect(QRectF(x, bottom - lv * h, barW, lv * h));
        const double hv = dbPos(hold);
        p.fillRect(QRectF(x, bottom - hv * h - 1, barW, 2), QColor("#ffffff"));
    };
    bar(x1, m_l, m_holdL);
    bar(x2, m_r, m_holdR);

    QFont f = p.font();
    f.setPixelSize(9);
    p.setFont(f);
    p.setPen(theme::textDim);
    for (int db : {0, -6, -12, -18, -24, -36, -48}) {
        const double y = bottom - (db + 60.0) / 60.0 * h;
        p.drawLine(QPointF(x1 - 4, y), QPointF(x1 - 1, y));
        p.drawText(QRectF(0, y - 6, x1 - 6, 12), Qt::AlignRight | Qt::AlignVCenter, QString::number(db));
    }
    p.drawText(QRect(x1, 10, 2 * barW + 4, 14), Qt::AlignCenter, "L  R");
}

}  // namespace hr
