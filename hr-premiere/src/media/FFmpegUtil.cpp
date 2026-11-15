#include "FFmpegUtil.h"

#include <QTransform>

namespace hr {

QString avErrorString(int err) {
    char buf[AV_ERROR_MAX_STRING_SIZE] = {0};
    av_strerror(err, buf, sizeof(buf));
    return QString::fromUtf8(buf);
}

QImage rotateImage(const QImage& img, int degrees) {
    degrees = ((degrees % 360) + 360) % 360;
    if (degrees == 0 || img.isNull()) return img;
    QTransform t;
    t.rotate(degrees);
    return img.transformed(t, Qt::SmoothTransformation);
}

}  // namespace hr
