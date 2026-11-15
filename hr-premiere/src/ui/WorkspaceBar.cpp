#include "core/I18n.h"
#include "WorkspaceBar.h"

#include "Icons.h"
#include "Theme.h"

#include <QFontMetricsF>
#include <QMouseEvent>
#include <QPainter>

namespace hr {

WorkspaceBar::WorkspaceBar(QWidget* parent) : QWidget(parent) {
    setFixedHeight(38);
    setMouseTracking(true);
}

void WorkspaceBar::setCurrent(const QString& name) {
    const int i = m_names.indexOf(name);
    if (i >= 0) { m_current = i; update(); }
}

void WorkspaceBar::setProjectName(const QString& n, bool dirty) {
    m_project = n;
    m_dirty = dirty;
    update();
}

QRectF WorkspaceBar::tabRect(int i) const {
    QFont f = font();
    f.setPixelSize(13);
    const QFontMetricsF fm(f);
    double x = 188;
    for (int k = 0; k < i; ++k) x += fm.horizontalAdvance(TR(m_names[k])) + 34;
    return QRectF(x, 0, fm.horizontalAdvance(TR(m_names[i])) + 34, height());
}

void WorkspaceBar::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setLayoutDirection(Qt::LeftToRight);   // painted LTR also in the Arabic version
    p.setRenderHint(QPainter::Antialiasing, true);
    p.fillRect(rect(), theme::bg2);
    p.setPen(theme::border);
    p.drawLine(0, height() - 1, width(), height() - 1);

    p.drawPixmap(QRect(12, 5, 28, 28), icons::logo(28));
    QFont brand = font();
    brand.setPixelSize(15);
    brand.setBold(true);
    p.setFont(brand);
    p.setPen(theme::text);
    p.drawText(QRectF(48, 0, 130, height()), Qt::AlignVCenter | Qt::AlignLeft, "HR Premiere");

    QFont f = font();
    f.setPixelSize(13);
    for (int i = 0; i < m_names.size(); ++i) {
        const QRectF r = tabRect(i);
        const bool cur = i == m_current;
        if (i == m_hover && !cur) p.fillRect(r.adjusted(2, 4, -2, -4), theme::bg4);
        p.setFont(f);
        p.setPen(cur ? (theme::dark ? QColor(Qt::white) : theme::text) : theme::textDim);     // readable on the light theme too
        p.drawText(r, Qt::AlignCenter, TR(m_names[i]));
        if (cur) {
            p.setPen(Qt::NoPen);
            p.setBrush(theme::accent);
            p.drawRoundedRect(QRectF(r.left() + 14, height() - 4, r.width() - 28, 3), 1.5, 1.5);
        }
    }
    p.setFont(f);
    p.setPen(theme::textDim);
    p.drawText(QRectF(0, 0, width() - 16, height()), Qt::AlignVCenter | Qt::AlignRight,
               QString("%1%2").arg(m_project, m_dirty ? " *" : ""));
}

void WorkspaceBar::mousePressEvent(QMouseEvent* e) {
    if (QRectF(8, 0, 170, height()).contains(e->position())) {
        emit logoClicked();
        return;
    }
    for (int i = 0; i < m_names.size(); ++i)
        if (tabRect(i).contains(e->position())) {
            m_current = i;
            update();
            emit workspaceSelected(m_names[i]);
            return;
        }
}

void WorkspaceBar::mouseMoveEvent(QMouseEvent* e) {
    int h = -1;
    for (int i = 0; i < m_names.size(); ++i)
        if (tabRect(i).contains(e->position())) h = i;
    if (h != m_hover) { m_hover = h; update(); }
    const bool onLogo = QRectF(8, 0, 170, height()).contains(e->position());
    setCursor(h >= 0 || onLogo ? Qt::PointingHandCursor : Qt::ArrowCursor);
    if (onLogo) setToolTip("الملكية · Ownership"); else setToolTip(QString());
}

void WorkspaceBar::leaveEvent(QEvent*) {
    m_hover = -1;
    update();
}

}  // namespace hr
