#include "versus/mixer/gpu_compositor.h"
#include <d3d11.h>
#include <d2d1_1.h>
#include <wrl.h>
#include <map>
#include <set>
#include <cstring>
using Microsoft::WRL::ComPtr;
namespace versus::mixer {
struct GpuCompositor::Impl {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<ID2D1Factory1> factory;
    ComPtr<ID2D1Device> d2d;
    ComPtr<ID2D1DeviceContext> painter;
    ComPtr<ID3D11Texture2D> target, staging;
    ComPtr<ID2D1Bitmap1> surface;
    struct Bitmap { qint64 key = 0; ComPtr<ID2D1Bitmap1> bitmap; QSize size; bool opaque = false; };
    std::vector<Bitmap> bitmaps;
    QSize size;
    QString error;
    bool initialized = false;
    bool init() {
        if (initialized) return painter != nullptr;
        initialized = true;
        HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
            nullptr, 0, D3D11_SDK_VERSION, &device, nullptr, &context);
        if (SUCCEEDED(hr)) hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory1), nullptr, reinterpret_cast<void**>(factory.GetAddressOf()));
        ComPtr<IDXGIDevice> dxgi;
        if (SUCCEEDED(hr)) hr = device.As(&dxgi);
        if (SUCCEEDED(hr)) hr = factory->CreateDevice(dxgi.Get(), &d2d);
        if (SUCCEEDED(hr)) hr = d2d->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &painter);
        if (FAILED(hr)) { error = "GPU compositor unavailable; using CPU."; painter.Reset(); return false; }
        return true;
    }
    bool resize(QSize dimensions) {
        if (size == dimensions && surface) return true;
        painter->SetTarget(nullptr); surface.Reset(); target.Reset(); staging.Reset();
        D3D11_TEXTURE2D_DESC desc{}; desc.Width = dimensions.width(); desc.Height = dimensions.height();
        desc.MipLevels = 1; desc.ArraySize = 1; desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM; desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT; desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        HRESULT hr = device->CreateTexture2D(&desc, nullptr, &target);
        desc.Usage = D3D11_USAGE_STAGING; desc.BindFlags = 0; desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        if (SUCCEEDED(hr)) hr = device->CreateTexture2D(&desc, nullptr, &staging);
        ComPtr<IDXGISurface> dxgi;
        if (SUCCEEDED(hr)) hr = target.As(&dxgi);
        const auto properties = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
        if (SUCCEEDED(hr)) hr = painter->CreateBitmapFromDxgiSurface(dxgi.Get(), &properties, &surface);
        if (FAILED(hr)) { error = "GPU surface allocation failed; using CPU."; return false; }
        painter->SetTarget(surface.Get()); size = dimensions; return true;
    }
};
GpuCompositor::GpuCompositor() : impl_(std::make_unique<Impl>()) {}
GpuCompositor::~GpuCompositor() = default;
QString GpuCompositor::error() const { return impl_->error; }
QImage GpuCompositor::compose(QSize size, QColor background, const std::vector<GpuLayer> &layers) {
    auto &s = *impl_;
    if (!s.init() || !s.resize(size)) return {};
    s.bitmaps.resize(layers.size());
    for (size_t i = 0; i < layers.size(); ++i) {
        auto &cached = s.bitmaps[i]; const auto &image = layers[i].image;
        if (cached.key == image.cacheKey() && cached.bitmap) continue;
        const bool opaque = image.format() == QImage::Format_RGB32;
        const QImage pixels = opaque ? image : image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
        HRESULT hr = S_OK;
        if (!cached.bitmap || cached.size != pixels.size() || cached.opaque != opaque) {
            cached.bitmap.Reset();
            const auto properties = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_NONE,
                D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, opaque ? D2D1_ALPHA_MODE_IGNORE : D2D1_ALPHA_MODE_PREMULTIPLIED));
            hr = s.painter->CreateBitmap(D2D1::SizeU(pixels.width(), pixels.height()), pixels.constBits(), pixels.bytesPerLine(), properties, &cached.bitmap);
        } else hr = cached.bitmap->CopyFromMemory(nullptr, pixels.constBits(), pixels.bytesPerLine());
        if (FAILED(hr)) { s.error = "GPU upload failed; using CPU."; return {}; }
        cached.key = image.cacheKey(); cached.size = pixels.size(); cached.opaque = opaque;
    }
    s.painter->BeginDraw();
    s.painter->Clear(D2D1::ColorF(background.redF(), background.greenF(), background.blueF(), background.alphaF()));
    for (size_t i = 0; i < layers.size(); ++i) {
        const auto &l = layers[i];
        const auto target = D2D1::RectF(l.target.left(), l.target.top(), l.target.right(), l.target.bottom());
        const auto crop = D2D1::RectF(l.crop.left(), l.crop.top(), l.crop.right(), l.crop.bottom());
        s.painter->DrawBitmap(s.bitmaps[i].bitmap.Get(), target, l.opacity, D2D1_INTERPOLATION_MODE_LINEAR, crop);
    }
    if (FAILED(s.painter->EndDraw())) { s.error = "GPU rendering interrupted; using CPU."; s.surface.Reset(); return {}; }
    s.context->CopyResource(s.staging.Get(), s.target.Get());
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(s.context->Map(s.staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) return {};
    QImage result(size, background.alpha() == 255 ? QImage::Format_RGB32 : QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < size.height(); ++y) std::memcpy(result.scanLine(y), static_cast<const char*>(mapped.pData)+y*mapped.RowPitch, size.width()*4);
    s.context->Unmap(s.staging.Get(), 0);
    return result;
}
}
