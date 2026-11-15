#include "core/I18n.h"
#include "MonitorPanel.h"

#include "Icons.h"
#include "Theme.h"
#include "core/EditOps.h"
#include "core/Util.h"

#include <QHBoxLayout>
#include <QMouseEvent>
#include <QPainter>
#include <QVBoxLayout>
#include <cmath>

namespace hr {

// ----------------------------------------------------------------- VideoView
VideoView::VideoView(QWidget* parent) : QWidget(parent) {
    setMinimumSize(160, 90);
    setAttribute(Qt::WA_OpaquePaintEvent);
}

void VideoView::setFrame(const QImage& img) {
    m_frame = img;
    update();
}
void VideoView::setSequenceSize(QSize s) {
    if (s.isEmpty() || s == m_seq) return;
    m_seq = s;
    update();
}
void VideoView::setPlaceholder(const QString& t) {
    m_placeholder = t;
    update();
}

QRectF VideoView::pictureRect() const {
    const QRectF avail = QRectF(rect()).adjusted(8, 8, -8, -8);
    const double ar = double(m_seq.width()) / m_seq.height();
    double w = avail.width(), h = w / ar;
    if (h > avail.height()) { h = avail.height(); w = h * ar; }
    return QRectF(avail.center().x() - w / 2, avail.center().y() - h / 2, w, h);
}

QSize VideoView::renderSize(double quality) const {
    const QRectF r = pictureRect();
    const double dpr = devicePixelRatioF();
    double w = std::min<double>(m_seq.width(), r.width() * dpr * quality);
    double h = w * m_seq.height() / m_seq.width();
    return QSize(std::max(16, int(w) & ~1), std::max(16, int(h) & ~1));
}

void VideoView::resizeEvent(QResizeEvent*) { emit resized(); }

void VideoView::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setLayoutDirection(Qt::LeftToRight);   // painted LTR also in the Arabic version
    p.fillRect(rect(), theme::bg1);
    const QRectF r = pictureRect();
    p.fillRect(r, Qt::black);
    if (!m_frame.isNull() && m_placeholder.isEmpty()) {
        p.setRenderHint(QPainter::SmoothPixmapTransform, true);
        p.drawImage(r, m_frame);
    }
    if (!m_placeholder.isEmpty()) {
        p.setPen(theme::textDim);
        p.drawText(r.adjusted(12, 12, -12, -12), Qt::AlignCenter | Qt::TextWordWrap, m_placeholder);
    }
    if (m_safe) {
        p.setPen(QPen(QColor(255, 255, 255, 90), 1, Qt::DashLine));
        p.drawRect(r.adjusted(r.width() * 0.05, r.height() * 0.05, -r.width() * 0.05, -r.height() * 0.05));
        p.drawRect(r.adjusted(r.width() * 0.1, r.height() * 0.1, -r.width() * 0.1, -r.height() * 0.1));
    }
}

// ----------------------------------------------------------------- MiniScrubber
MiniScrubber::MiniScrubber(QWidget* parent) : QWidget(parent) {
    setFixedHeight(22);
    setCursor(Qt::PointingHandCursor);
}
double MiniScrubber::xToTime(double x) const {
    const double w = std::max(1, width() - 16);
    return std::max(0.0, std::min(m_duration, (x - 8) / w * m_duration));
}
void MiniScrubber::mousePressEvent(QMouseEvent* e) { emit seekRequested(xToTime(e->position().x())); }
void MiniScrubber::mouseMoveEvent(QMouseEvent* e) {
    if (e->buttons() & Qt::LeftButton) emit seekRequested(xToTime(e->position().x()));
}
void MiniScrubber::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setLayoutDirection(Qt::LeftToRight);   // painted LTR also in the Arabic version
    p.setRenderHint(QPainter::Antialiasing, true);
    const double w = width() - 16;
    const QRectF bar(8, height() / 2.0 - 2, w, 4);
    p.setPen(Qt::NoPen);
    p.setBrush(theme::bg3);
    p.drawRoundedRect(bar, 2, 2);
    auto px = [&](double t) { return 8 + (m_duration > 0 ? t / m_duration * w : 0); };
    if (m_in >= 0 || m_out >= 0) {
        const double a = m_in >= 0 ? px(m_in) : 8, b = m_out >= 0 ? px(m_out) : 8 + w;
        p.setBrush(theme::accentDim);
        p.drawRoundedRect(QRectF(a, bar.top() - 1, std::max(2.0, b - a), 6), 2, 2);
    }
    const double x = px(m_pos);
    p.setBrush(theme::accent);
    p.drawRoundedRect(QRectF(8, bar.top(), std::max(0.0, x - 8), 4), 2, 2);
    p.setBrush(theme::accentHi);
    p.drawEllipse(QPointF(x, height() / 2.0), 6, 6);
}

// ----------------------------------------------------------------- MonitorPanel
QToolButton* MonitorPanel::button(int icon, const QString& tip) {
    auto* b = new QToolButton(this);
    b->setIcon(icons::make(icons::Name(icon)));
    b->setIconSize(QSize(20, 20));
    b->setToolTip(tip);
    b->setAutoRaise(true);
    return b;
}

MonitorPanel::MonitorPanel(Player* player, Kind kind, QWidget* parent)
    : QWidget(parent), m_player(player), m_kind(kind) {
    m_view = new VideoView(this);
    m_scrub = new MiniScrubber(this);
    m_tc = new QLabel("00:00:00:00", this);
    m_tc->setObjectName("monitorTime");
    m_dur = new QLabel("00:00:00:00", this);
    m_dur->setObjectName("monitorDuration");
    m_quality = new QComboBox(this);
    m_quality->addItem(TR("Full"), 1.0);
    m_quality->addItem("1/2", 0.5);
    m_quality->addItem("1/4", 0.25);
    m_quality->setToolTip(TR("Playback resolution"));
    m_quality->setFixedWidth(70);
    m_quality->setCurrentIndex(0);

    auto* top = new QHBoxLayout;
    top->setContentsMargins(10, 0, 10, 0);
    top->addWidget(m_tc);
    top->addStretch();
    top->addWidget(m_quality);
    top->addStretch();
    top->addWidget(m_dur);

    auto* markIn = button(icons::MarkIn, TR("Mark In (I)"));
    auto* markOut = button(icons::MarkOut, TR("Mark Out (O)"));
    auto* toStart = button(icons::ToStart, m_kind == Program ? TR("Go to Previous Edit (Up)") : TR("Go to Start (Home)"));
    auto* stepBack = button(icons::StepBack, TR("Step Back 1 Frame (Left)"));
    m_play = button(icons::Play, TR("Play / Stop (Space)"));
    m_play->setIconSize(QSize(26, 26));
    auto* stepFwd = button(icons::StepForward, TR("Step Forward 1 Frame (Right)"));
    auto* toEnd = button(icons::ToEnd, m_kind == Program ? TR("Go to Next Edit (Down)") : TR("Go to End (End)"));

    auto* bar = new QHBoxLayout;
    bar->setContentsMargins(6, 0, 6, 6);
    bar->addStretch();
    bar->addWidget(markIn);
    bar->addWidget(markOut);
    bar->addSpacing(14);
    bar->addWidget(toStart);
    bar->addWidget(stepBack);
    bar->addWidget(m_play);
    bar->addWidget(stepFwd);
    bar->addWidget(toEnd);
    bar->addSpacing(14);
    if (m_kind == Source) {
        auto* ins = button(icons::Insert, TR("Insert (,)"));
        auto* ovr = button(icons::Overwrite, TR("Overwrite (.)"));
        bar->addWidget(ins);
        bar->addWidget(ovr);
        connect(ins, &QToolButton::clicked, this, &MonitorPanel::insertRequested);
        connect(ovr, &QToolButton::clicked, this, &MonitorPanel::overwriteRequested);
    }
    auto* cam = button(icons::Camera, TR("Export Frame (Ctrl+Shift+E)"));
    bar->addWidget(cam);
    bar->addStretch();

    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(4);
    lay->addWidget(m_view, 1);
    lay->addWidget(m_scrub);
    lay->addLayout(top);
    lay->addLayout(bar);

    connect(m_play, &QToolButton::clicked, this, [this] { emit interacted(); m_player->toggle(); });
    connect(stepBack, &QToolButton::clicked, this, [this] { emit interacted(); m_player->stepFrames(-1); });
    connect(stepFwd, &QToolButton::clicked, this, [this] { emit interacted(); m_player->stepFrames(1); });
    connect(toStart, &QToolButton::clicked, this, [this] {
        emit interacted();
        m_player->seek(m_kind == Program ? ops::prevEdit(m_player->timeline(), m_player->position()) : 0.0);
    });
    connect(toEnd, &QToolButton::clicked, this, [this] {
        emit interacted();
        m_player->seek(m_kind == Program ? ops::nextEdit(m_player->timeline(), m_player->position()) : m_player->duration());
    });
    connect(markIn, &QToolButton::clicked, this, [this] { emit markInRequested(m_player->position()); });
    connect(markOut, &QToolButton::clicked, this, [this] { emit markOutRequested(m_player->position()); });
    connect(cam, &QToolButton::clicked, this, &MonitorPanel::exportFrameRequested);
    connect(m_scrub, &MiniScrubber::seekRequested, this, [this](double t) { emit interacted(); m_player->seek(t); });
    connect(m_player, &Player::positionChanged, this, &MonitorPanel::onPosition);
    connect(m_player, &Player::frameReady, this, [this](const QImage& img, double) { m_view->setFrame(img); });
    connect(m_player, &Player::playingChanged, this, [this](bool playing) {
        m_play->setIcon(icons::make(playing ? icons::Pause : icons::Play));
    });
    connect(m_view, &VideoView::resized, this, &MonitorPanel::applyViewSize);
    connect(m_quality, &QComboBox::currentIndexChanged, this, &MonitorPanel::applyViewSize);
    m_view->installEventFilter(this);
    applyViewSize();
}

bool MonitorPanel::eventFilter(QObject* o, QEvent* e) {
    if (o == m_view && e->type() == QEvent::MouseButtonPress) emit interacted();
    return QWidget::eventFilter(o, e);
}

void MonitorPanel::applyViewSize() {
    m_player->setViewSize(m_view->renderSize(m_quality->currentData().toDouble()));
    m_player->refresh();
}

void MonitorPanel::setSequenceSize(QSize s) {
    m_view->setSequenceSize(s);
    applyViewSize();
}

void MonitorPanel::setMarks(double in, double out) { m_scrub->setMarks(in, out); }

void MonitorPanel::syncDuration() {
    m_scrub->setDuration(m_player->duration());
    m_dur->setText(formatTimecode(m_player->duration(), m_player->fps()));
    onPosition(m_player->position());
}

void MonitorPanel::onPosition(double t) {
    m_tc->setText(formatTimecode(t, m_player->fps()));
    m_scrub->setPosition(t);
}

}  // namespace hr
