#pragma once
#include <QWidget>

namespace hr {

// Stereo peak meters with a dB scale (like Premiere's Audio Meters panel).
class AudioMeters : public QWidget {
    Q_OBJECT
public:
    explicit AudioMeters(QWidget* parent = nullptr);
    void setLevels(float left, float right);

protected:
    void paintEvent(QPaintEvent*) override;

private:
    float m_l = 0, m_r = 0, m_holdL = 0, m_holdR = 0;
};

}  // namespace hr
