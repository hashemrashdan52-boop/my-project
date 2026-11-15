#include "Util.h"

#include <QRegularExpression>
#include <QStringList>

namespace hr {

QString formatTimecode(double seconds, double fps) {
    if (fps <= 0) fps = 30;
    if (seconds < 0) seconds = 0;
    const int nominal = std::max(1, int(std::lround(fps)));
    const qint64 frames = qint64(std::floor(seconds * fps + 1e-6));
    const qint64 ff = frames % nominal;
    qint64 total = frames / nominal;
    const qint64 ss = total % 60; total /= 60;
    const qint64 mm = total % 60;
    const qint64 hh = total / 60;
    return QString("%1:%2:%3:%4")
        .arg(hh, 2, 10, QLatin1Char('0'))
        .arg(mm, 2, 10, QLatin1Char('0'))
        .arg(ss, 2, 10, QLatin1Char('0'))
        .arg(ff, 2, 10, QLatin1Char('0'));
}

double parseTimecode(const QString& textIn, double fps, bool* ok) {
    if (ok) *ok = false;
    QString text = textIn.trimmed();
    if (text.isEmpty()) return 0;
    if (text.endsWith('s', Qt::CaseInsensitive)) {
        bool good = false;
        double v = text.chopped(1).toDouble(&good);
        if (ok) *ok = good;
        return good ? v : 0;
    }
    const QStringList parts = text.split(QRegularExpression("[:;.]"), Qt::SkipEmptyParts);
    if (parts.size() == 1) {
        bool good = false;
        double frames = parts[0].toDouble(&good);
        if (ok) *ok = good;
        return good ? frames / fps : 0;
    }
    double total = 0;
    // last part = frames, then seconds, minutes, hours
    double mult[4] = {1.0 / fps, 1, 60, 3600};
    for (int i = 0; i < parts.size() && i < 4; ++i) {
        bool good = false;
        double v = parts[parts.size() - 1 - i].toDouble(&good);
        if (!good) return 0;
        total += v * mult[i];
    }
    if (ok) *ok = true;
    return total;
}

QString formatDuration(double seconds) {
    if (seconds < 0) seconds = 0;
    qint64 s = qint64(std::llround(seconds));
    const qint64 h = s / 3600; s %= 3600;
    const qint64 m = s / 60; s %= 60;
    if (h > 0)
        return QString("%1:%2:%3").arg(h).arg(m, 2, 10, QLatin1Char('0')).arg(s, 2, 10, QLatin1Char('0'));
    return QString("%1:%2").arg(m).arg(s, 2, 10, QLatin1Char('0'));
}

double dbToGain(double db) {
    if (db <= -60.0) return 0.0;
    return std::pow(10.0, db / 20.0);
}

}  // namespace hr
