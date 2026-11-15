#pragma once
#include <QImage>

// CPU implementations of the video effects.  All functions work in place on
// 32-bit images (Format_RGB32 or Format_ARGB32_Premultiplied).
namespace hr::fx {

void brightnessContrast(QImage& img, double brightness, double contrast);
void colorCorrection(QImage& img, double exposure, double saturation, double temperature, double tint);
void blackWhite(QImage& img, double amount);
void invert(QImage& img, double amount);
void gaussianBlur(QImage& img, double sigmaPx);
void sharpen(QImage& img, double amount, double sigmaPx);
void vignette(QImage& img, double amount, double size);

}  // namespace hr::fx
