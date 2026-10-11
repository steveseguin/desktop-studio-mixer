#pragma once
#include <QColor>
#include <QImage>
#include <QRectF>
#include <memory>
#include <vector>
namespace versus::mixer {
struct GpuLayer { QImage image; QRectF target, crop; float opacity = 1; };
// Direct2D composites on a D3D11 device. Readback remains necessary for the
// existing encoder/display interfaces; callers keep the CPU fallback.
class GpuCompositor {
public:
    GpuCompositor();
    ~GpuCompositor();
    QImage compose(QSize size, QColor background, const std::vector<GpuLayer> &layers);
    QString error() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
