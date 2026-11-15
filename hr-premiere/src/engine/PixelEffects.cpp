#include "PixelEffects.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace hr::fx {

static inline int clamp255(float v) { return v < 0 ? 0 : (v > 255 ? 255 : int(v + 0.5f)); }

template <typename F>
static void forEachPixel(QImage& img, F f) {
    const int w = img.width(), h = img.height();
    for (int y = 0; y < h; ++y) {
        uint32_t* p = reinterpret_cast<uint32_t*>(img.scanLine(y));
        for (int x = 0; x < w; ++x) p[x] = f(x, y, p[x]);
    }
}
#define A_(c) ((c) >> 24)
#define R_(c) (((c) >> 16) & 0xff)
#define G_(c) (((c) >> 8) & 0xff)
#define B_(c) ((c) & 0xff)
#define PACK(a, r, g, b) ((uint32_t(a) << 24) | (uint32_t(r) << 16) | (uint32_t(g) << 8) | uint32_t(b))

void brightnessContrast(QImage& img, double brightness, double contrast) {
    if (brightness == 0 && contrast == 0) return;
    const float add = float(brightness * 2.55);
    const double c255 = contrast * 2.55;
    const float f = float((259.0 * (c255 + 255.0)) / (255.0 * (259.0 - c255)));
    uint8_t lut[256];
    for (int i = 0; i < 256; ++i) lut[i] = uint8_t(clamp255(f * (i - 128) + 128 + add));
    forEachPixel(img, [&](int, int, uint32_t c) { return PACK(A_(c), lut[R_(c)], lut[G_(c)], lut[B_(c)]); });
}

void colorCorrection(QImage& img, double exposure, double saturation, double temperature, double tint) {
    if (exposure == 0 && saturation == 100 && temperature == 0 && tint == 0) return;
    const float ex = float(std::pow(2.0, exposure));
    const float gr = ex * float(1.0 + 0.25 * temperature / 100.0);
    const float gg = ex * float(1.0 - 0.15 * tint / 100.0);
    const float gb = ex * float(1.0 - 0.25 * temperature / 100.0);
    const float sat = float(saturation / 100.0);
    forEachPixel(img, [&](int, int, uint32_t c) {
        float r = R_(c) * gr, g = G_(c) * gg, b = B_(c) * gb;
        const float l = 0.299f * r + 0.587f * g + 0.114f * b;
        r = l + (r - l) * sat;
        g = l + (g - l) * sat;
        b = l + (b - l) * sat;
        return PACK(A_(c), clamp255(r), clamp255(g), clamp255(b));
    });
}

void blackWhite(QImage& img, double amount) {
    const float a = float(amount / 100.0);
    if (a <= 0) return;
    forEachPixel(img, [&](int, int, uint32_t c) {
        const float r = R_(c), g = G_(c), b = B_(c);
        const float l = 0.299f * r + 0.587f * g + 0.114f * b;
        return PACK(A_(c), clamp255(r + (l - r) * a), clamp255(g + (l - g) * a), clamp255(b + (l - b) * a));
    });
}

void invert(QImage& img, double amount) {
    const float a = float(amount / 100.0);
    if (a <= 0) return;
    forEachPixel(img, [&](int, int, uint32_t c) {
        const float r = R_(c), g = G_(c), b = B_(c);
        return PACK(A_(c), clamp255(r + (255 - 2 * r) * a), clamp255(g + (255 - 2 * g) * a),
                    clamp255(b + (255 - 2 * b) * a));
    });
}

// ---------------------------------------------------------------- blur
static void boxH(const uint8_t* src, uint8_t* dst, int w, int h, int stride, int r) {
    const int n = 2 * r + 1;
    for (int y = 0; y < h; ++y) {
        const uint8_t* s = src + size_t(y) * stride;
        uint8_t* d = dst + size_t(y) * stride;
        int sum[4] = {0, 0, 0, 0};
        for (int i = -r; i <= r; ++i) {
            const int idx = std::min(w - 1, std::max(0, i));
            for (int c = 0; c < 4; ++c) sum[c] += s[idx * 4 + c];
        }
        for (int x = 0; x < w; ++x) {
            for (int c = 0; c < 4; ++c) d[x * 4 + c] = uint8_t((sum[c] + n / 2) / n);
            const int addI = std::min(w - 1, x + r + 1), remI = std::max(0, x - r);
            for (int c = 0; c < 4; ++c) sum[c] += s[addI * 4 + c] - s[remI * 4 + c];
        }
    }
}

static void boxV(const uint8_t* src, uint8_t* dst, int w, int h, int stride, int r) {
    const int n = 2 * r + 1;
    std::vector<int> col(size_t(w) * 4, 0);
    for (int i = -r; i <= r; ++i) {
        const uint8_t* s = src + size_t(std::min(h - 1, std::max(0, i))) * stride;
        for (int k = 0; k < w * 4; ++k) col[k] += s[k];
    }
    for (int y = 0; y < h; ++y) {
        uint8_t* d = dst + size_t(y) * stride;
        for (int k = 0; k < w * 4; ++k) d[k] = uint8_t((col[k] + n / 2) / n);
        const uint8_t* a = src + size_t(std::min(h - 1, y + r + 1)) * stride;
        const uint8_t* b = src + size_t(std::max(0, y - r)) * stride;
        for (int k = 0; k < w * 4; ++k) col[k] += a[k] - b[k];
    }
}

void gaussianBlur(QImage& img, double sigma) {
    if (sigma < 0.4 || img.isNull()) return;
    const int w = img.width(), h = img.height(), stride = int(img.bytesPerLine());
    int r = int(std::lround(std::sqrt(sigma * sigma + 0.25) - 0.5));
    r = std::max(1, std::min(r, std::min(w, h) / 2 - 1));
    if (r < 1) return;
    QImage tmp(w, h, img.format());
    for (int pass = 0; pass < 3; ++pass) {
        boxH(img.constBits(), tmp.bits(), w, h, stride, r);
        boxV(tmp.constBits(), img.bits(), w, h, stride, r);
    }
}

void sharpen(QImage& img, double amount, double sigma) {
    if (amount <= 0 || img.isNull()) return;
    QImage blurred = img.copy();
    gaussianBlur(blurred, std::max(0.8, sigma));
    const float k = float(amount / 100.0 * 2.0);
    const int w = img.width(), h = img.height();
    for (int y = 0; y < h; ++y) {
        uint32_t* p = reinterpret_cast<uint32_t*>(img.scanLine(y));
        const uint32_t* q = reinterpret_cast<const uint32_t*>(blurred.constScanLine(y));
        for (int x = 0; x < w; ++x) {
            const uint32_t c = p[x], b = q[x];
            p[x] = PACK(A_(c), clamp255(R_(c) + (int(R_(c)) - int(R_(b))) * k),
                        clamp255(G_(c) + (int(G_(c)) - int(G_(b))) * k),
                        clamp255(B_(c) + (int(B_(c)) - int(B_(b))) * k));
        }
    }
}

void vignette(QImage& img, double amount, double size) {
    if (amount <= 0) return;
    const int w = img.width(), h = img.height();
    const float cx = (w - 1) * 0.5f, cy = (h - 1) * 0.5f;
    const float inner = float(size / 150.0 * 0.9);
    const float a = float(amount / 100.0);
    forEachPixel(img, [&](int x, int y, uint32_t c) {
        const float nx = (x - cx) / cx, ny = (y - cy) / cy;
        const float rad = std::sqrt(nx * nx + ny * ny) / 1.41421356f;
        float t = (rad - inner) / std::max(0.01f, 1.0f - inner);
        t = t < 0 ? 0 : (t > 1 ? 1 : t);
        t = t * t * (3 - 2 * t);
        const float f = 1.0f - a * t;
        return PACK(A_(c), clamp255(R_(c) * f), clamp255(G_(c) * f), clamp255(B_(c) * f));
    });
}

}  // namespace hr::fx
