#pragma once

#include "versus/video/window_capture.h"
#include <QImage>
#include <limits>

namespace versus::mixer {
// Keep the capture buffer alive until Qt releases its last image reference.
// The const-data constructor forces a detach if a consumer attempts to edit it.
inline QImage takeFrameImage(video::CapturedFrame frame) {
    if (frame.format != video::CapturedFrame::Format::BGRA || frame.width <= 0 || frame.height <= 0 ||
        frame.width > std::numeric_limits<int>::max()/4 || frame.stride < frame.width*4 ||
        frame.data.size()/static_cast<size_t>(frame.stride) < static_cast<size_t>(frame.height)) return {};
    auto *pixels = new std::vector<uint8_t>(std::move(frame.data));
    return QImage(static_cast<const uchar*>(pixels->data()), frame.width, frame.height, frame.stride, QImage::Format_ARGB32,
        [](void *data) { delete static_cast<std::vector<uint8_t>*>(data); }, pixels);
}
} // namespace versus::mixer
