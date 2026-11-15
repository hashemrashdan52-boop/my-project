#pragma once
#include <QImage>
#include <QString>

extern "C" {
#include <libavutil/error.h>
}

namespace hr {

QString avErrorString(int err);
// Rotates an image by 0/90/180/270 degrees clockwise.
QImage rotateImage(const QImage& img, int degrees);

}  // namespace hr
