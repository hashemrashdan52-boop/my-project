#pragma once
#include <QLabel>

namespace hr {

// Label whose value can be changed by dragging it left / right, like the blue
// numbers in Premiere's Effect Controls.
class ScrubLabel : public QLabel {
    Q_OBJECT
public:
    explicit ScrubLabel(const QString& text, QWidget* parent = nullptr);
    void setScrubRange(double min, double max, double step) { m_min = min; m_max = max; m_step = step; }
    void setValue(double v) { m_value = v; }

signals:
    void scrubbed(double value);

protected:
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;

private:
    double m_min = 0, m_max = 100, m_step = 1, m_value = 0, m_startValue = 0;
    int m_pressX = 0;
    bool m_dragging = false;
};

}  // namespace hr
