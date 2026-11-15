#pragma once
#include <QColor>
#include <QPainter>
#include <QPointF>
#include <QRandomGenerator>
#include <QSizeF>
#include <QVector>
#include <algorithm>
#include <cmath>

namespace hr {

constexpr double kParticlePi = 3.14159265358979323846;

// The animated background of the Ownership page (a Qt port of the WinForms particle code): drifting dots
// that bounce off the edges, fade out towards the bottom and are joined by thin lines when two of them
// come closer than 50 px.
class ParticleField {
public:
    struct Particle {
        QPointF pos;
        QPointF vel;
        int radius = 2;
    };

    explicit ParticleField(int count = 150, QColor color = QColor(255, 0, 0)) : m_color(color), m_count(count) {}

    void resize(QSizeF size) {
        if (size.width() <= 0 || size.height() <= 0) return;
        if (m_particles.isEmpty()) {
            m_size = size;
            init();
            return;
        }
        for (Particle& p : m_particles)       // keep every dot at the same relative place
            p.pos = QPointF(p.pos.x() * size.width() / m_size.width(), p.pos.y() * size.height() / m_size.height());
        m_size = size;
    }

    // dtMs = time since the last step; the original code moved the dots every ~15.6 ms timer tick
    void update(double dtMs) {
        const double k = std::min(dtMs, 100.0) / 15.6;
        for (Particle& p : m_particles) {
            p.pos += p.vel * 0.5 * k;
            if (p.pos.x() < 0 || p.pos.x() > m_size.width()) {
                p.vel.setX(-p.vel.x());
                p.pos.setX(std::max(0.0, std::min(p.pos.x(), m_size.width())));
            }
            if (p.pos.y() < 0 || p.pos.y() > m_size.height()) {
                p.vel.setY(-p.vel.y());
                p.pos.setY(std::max(0.0, std::min(p.pos.y(), m_size.height())));
            }
        }
    }

    void paint(QPainter& painter) const {
        if (m_particles.isEmpty() || m_size.height() <= 0) return;
        painter.save();
        painter.setRenderHint(QPainter::Antialiasing, true);

        // dots: fully visible at the top, transparent at the bottom
        painter.setPen(Qt::NoPen);
        for (const Particle& p : m_particles) {
            const int alpha = std::max(0, std::min(255, int((1.0 - p.pos.y() / m_size.height()) * 255.0)));
            QColor c = m_color;
            c.setAlpha(alpha);
            painter.setBrush(c);
            const double r = double(p.radius / 2);
            painter.drawEllipse(p.pos, r, r);
        }

        // lines between close neighbours (the original draws every pair twice, which doubles the alpha)
        const int n = int(m_particles.size());
        for (int i = 0; i < n; ++i) {
            for (int j = i + 1; j < n; ++j) {
                const double dx = m_particles[i].pos.x() - m_particles[j].pos.x();
                const double dy = m_particles[i].pos.y() - m_particles[j].pos.y();
                const double d2 = dx * dx + dy * dy;
                if (d2 >= 2500.0) continue;
                const double a = 1.0 - std::sqrt(d2) / 50.0;
                QColor c = m_color;
                c.setAlpha(int((1.0 - (1.0 - a) * (1.0 - a)) * 255.0));
                painter.setPen(QPen(c, 1));
                painter.drawLine(m_particles[i].pos, m_particles[j].pos);
            }
        }
        painter.restore();
    }

    int count() const { return int(m_particles.size()); }
    const QVector<Particle>& particles() const { return m_particles; }
    QSizeF size() const { return m_size; }

private:
    void init() {
        m_particles.clear();
        auto* rng = QRandomGenerator::global();
        for (int i = 0; i < m_count; ++i) {
            const double angle = rng->generateDouble() * 2.0 * kParticlePi;
            const double speed = double(rng->bounded(1, 3));   // 1 or 2
            Particle p;
            p.pos = QPointF(rng->generateDouble() * m_size.width(), rng->generateDouble() * m_size.height());
            p.vel = QPointF(std::cos(angle) * speed, std::sin(angle) * speed);
            p.radius = rng->bounded(2, 5);                      // 2..4
            m_particles.push_back(p);
        }
    }

    QVector<Particle> m_particles;
    QSizeF m_size{0, 0};
    QColor m_color;
    int m_count;
};

}  // namespace hr
