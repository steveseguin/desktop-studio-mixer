// Pixel/ownership gate for the opaque-frame shortcuts. Actual desktop workflows
// and decoded recordings are verified separately by the mixer e2e scripts.
#include "versus/mixer/frame_image.h"
#include <QPainter>
#include <cstring>
#include <iostream>
#include <random>

int main() {
    std::mt19937 random(417);
    size_t comparisons = 0;
    for (const auto size : {QSize(1280, 720), QSize(193, 109), QSize(64, 48)}) {
        versus::video::CapturedFrame captured;
        captured.width = size.width(); captured.height = size.height();
        captured.stride = size.width()*4;
        captured.data.resize(static_cast<size_t>(captured.stride)*size.height());
        for (size_t p = 0; p < captured.data.size(); p += 4) {
            captured.data[p] = static_cast<uint8_t>(random());
            captured.data[p+1] = static_cast<uint8_t>(random());
            captured.data[p+2] = static_cast<uint8_t>(random());
            captured.data[p+3] = 255;
        }
        const auto original = captured.data;
        auto unknown = versus::mixer::takeFrameImage(captured);
        captured.opaque = true;
        auto opaque = versus::mixer::takeFrameImage(std::move(captured));
        if (!unknown.hasAlphaChannel() || opaque.hasAlphaChannel() ||
            std::memcmp(opaque.constBits(), original.data(), original.size())) return 1;
        auto detached = opaque;
        detached.fill(Qt::red);
        if (std::memcmp(opaque.constBits(), original.data(), original.size())) return 2;

        // Same-size full-canvas bypass must equal the original painter path,
        // including an imported background that itself contains transparency.
        for (const int alpha : {0, 91, 255}) {
            QImage reference(size, QImage::Format_ARGB32_Premultiplied);
            reference.fill(QColor(20, 35, 50, alpha));
            QPainter p(&reference); p.setRenderHint(QPainter::SmoothPixmapTransform);
            p.drawImage(QRectF(QPointF(0, 0), QSizeF(size)), unknown); p.end();
            if (std::memcmp(reference.constBits(), opaque.constBits(), original.size())) return 3;
            ++comparisons;
        }
        QImage overlay(size, QImage::Format_ARGB32);
        for (int y = 0; y < overlay.height(); ++y)
            for (int x = 0; x < overlay.width(); ++x) overlay.setPixel(x, y, random());
        // Opaque destination metadata must not alter transparent overlay,
        // aspect-fit, fractional geometry, opacity or fade rounding.
        for (const auto rect : {QRectF(QPointF(0, 0), QSizeF(size)), QRectF(11.3, 9.7, 81.9, 59.1), QRectF(-3.6, 23.4, 210.2, 130.7)}) {
            for (const double opacity : {0., .01, .25, .6, 1.}) {
                QImage before(size, QImage::Format_ARGB32_Premultiplied);
                QImage after(size, QImage::Format_RGB32);
                before.fill(QColor(16, 23, 34)); after.fill(QColor(16, 23, 34));
                for (int variant = 0; variant < 2; ++variant) {
                    QPainter p(variant ? &after : &before);
                    p.setRenderHint(QPainter::SmoothPixmapTransform);
                    p.setOpacity(opacity);
                    QImage straightView(opaque.constBits(), opaque.width(), opaque.height(), opaque.bytesPerLine(), QImage::Format_ARGB32);
                    p.drawImage(rect, variant ? straightView : unknown);
                    p.setOpacity(.67); p.drawImage(rect.translated(7.2, -8.9), overlay);
                    p.setOpacity(.39); p.drawImage(QPointF(0, 0), overlay.convertToFormat(QImage::Format_ARGB32_Premultiplied));
                    // Fade/composite images used to be premultiplied ARGB.
                    // Opaque RGB storage must preserve that rounding too.
                    p.setOpacity(opacity);
                    p.drawImage(rect, variant ? opaque : unknown.convertToFormat(QImage::Format_ARGB32_Premultiplied));
                }
                if (std::memcmp(before.constBits(), after.constBits(), original.size())) {
                    std::cerr << "Pixel mismatch at " << size.width() << " opacity " << opacity << '\n';
                    return 4;
                }
                ++comparisons;
            }
        }
    }
    std::cout << comparisons << " exact pixel comparisons and capture ownership checks passed\n";
}
