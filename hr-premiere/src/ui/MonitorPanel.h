#pragma once
#include "engine/Player.h"

#include <QComboBox>
#include <QLabel>
#include <QToolButton>
#include <QWidget>

namespace hr {

// The picture itself.
class VideoView : public QWidget {
    Q_OBJECT
public:
    explicit VideoView(QWidget* parent = nullptr);
    void setFrame(const QImage& img);
    void setSequenceSize(QSize s);
    void setPlaceholder(const QString& text);
    void setSafeMargins(bool on) { m_safe = on; update(); }
    QSize renderSize(double quality) const;   // pixels to render for the current widget size
    QImage frame() const { return m_frame; }

signals:
    void resized();

protected:
    void paintEvent(QPaintEvent*) override;
    void resizeEvent(QResizeEvent*) override;

private:
    QRectF pictureRect() const;
    QImage m_frame;
    QSize m_seq{1920, 1080};
    QString m_placeholder;
    bool m_safe = false;
};

// The thin scrub bar under the picture.
class MiniScrubber : public QWidget {
    Q_OBJECT
public:
    explicit MiniScrubber(QWidget* parent = nullptr);
    void setDuration(double d) { m_duration = d; update(); }
    void setPosition(double t) { m_pos = t; update(); }
    void setMarks(double in, double out) { m_in = in; m_out = out; update(); }

signals:
    void seekRequested(double t);

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;

private:
    double xToTime(double x) const;
    double m_duration = 0, m_pos = 0, m_in = -1, m_out = -1;
};

class MonitorPanel : public QWidget {
    Q_OBJECT
public:
    enum Kind { Source, Program };
    MonitorPanel(Player* player, Kind kind, QWidget* parent = nullptr);

    Player* player() const { return m_player; }
    VideoView* view() { return m_view; }
    void setSequenceSize(QSize s);
    void setMarks(double in, double out);
    void setPlaceholder(const QString& t) { m_view->setPlaceholder(t); }
    void syncDuration();

signals:
    void markInRequested(double t);
    void markOutRequested(double t);
    void insertRequested();
    void overwriteRequested();
    void exportFrameRequested();
    void interacted();

protected:
    bool eventFilter(QObject* o, QEvent* e) override;

private slots:
    void onPosition(double t);
    void applyViewSize();

private:
    QToolButton* button(int icon, const QString& tip);

    Player* m_player;
    Kind m_kind;
    VideoView* m_view;
    MiniScrubber* m_scrub;
    QLabel* m_tc;
    QLabel* m_dur;
    QComboBox* m_quality;
    QToolButton* m_play;
};

}  // namespace hr
