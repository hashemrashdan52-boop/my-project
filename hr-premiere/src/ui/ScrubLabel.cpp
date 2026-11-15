#include "ScrubLabel.h"

#include <QMouseEvent>
#include <algorithm>
#include <cmath>

namespace hr {

ScrubLabel::ScrubLabel(const QString& text, QWidget* parent) : QLabel(text, parent) {
    setCursor(Qt::SizeHorCursor);
    setToolTip("Drag left or right to change the value");
}

void ScrubLabel::mousePressEvent(QMouseEvent* e) {
    if (e->button() != Qt::LeftButton) return;
    m_dragging = true;
    m_pressX = int(e->position().x());
    m_startValue = m_value;
}

void ScrubLabel::mouseMoveEvent(QMouseEvent* e) {
    if (!m_dragging) return;
    double dx = e->position().x() - m_pressX;
    double mult = (e->modifiers() & Qt::ShiftModifier) ? 10.0 : ((e->modifiers() & Qt::ControlModifier) ? 0.1 : 1.0);
    double v = m_startValue + dx * m_step * mult;
    v = std::max(m_min, std::min(m_max, v));
    m_value = v;
    emit scrubbed(v);
}

void ScrubLabel::mouseReleaseEvent(QMouseEvent*) { m_dragging = false; }

}  // namespace hr
