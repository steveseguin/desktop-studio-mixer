#include "versus/mixer/browser_source.h"
#include <QCloseEvent>
#include <QApplication>
#include <QDesktopServices>
#include <QDir>
#include <QPointer>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>
#ifdef _WIN32
#include <windows.h>
#include <wrl.h>
#include <WebView2.h>
#include <WebView2EnvironmentOptions.h>
using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;
#endif

namespace versus::mixer {
struct BrowserSource::Impl {
    QTimer timer;
    bool busy = false;
#ifdef _WIN32
    ComPtr<ICoreWebView2Controller> controller;
    ComPtr<ICoreWebView2> web;
#endif
};

BrowserSource::BrowserSource(const QString &id, const QUrl &url, bool renderFrames)
    : QWidget(nullptr, Qt::Tool), impl_(std::make_unique<Impl>()) {
    setWindowTitle("Video Mixer - Browser source");
    setAttribute(Qt::WA_QuitOnClose, false);
    resize(1280, 720);
    park();
    QPointer<BrowserSource> self(this);
    QTimer::singleShot(0, this, [self, id, url, renderFrames] {
        if (!self) return;
#ifdef _WIN32
        const auto profile = QDir(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation))
            .filePath("VideoMixer/browser/" + id);
        QDir().mkpath(profile);
        auto options = Microsoft::WRL::Make<CoreWebView2EnvironmentOptions>();
        options->put_AdditionalBrowserArguments(L"--autoplay-policy=no-user-gesture-required --disable-backgrounding-occluded-windows --disable-renderer-backgrounding");
        const HRESULT hr = CreateCoreWebView2EnvironmentWithOptions(nullptr,
            profile.toStdWString().c_str(), options.Get(),
            Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
                [self, url, renderFrames](HRESULT result, ICoreWebView2Environment *environment) -> HRESULT {
                    if (!self) return S_OK;
                    if (FAILED(result) || !environment) {
                        emit self->errorOccurred("Browser sources need Microsoft Edge WebView2 Evergreen Runtime. Install it from microsoft.com/edge/webview2, then reopen the source.");
                        return S_OK;
                    }
                    const HRESULT created = environment->CreateCoreWebView2Controller(
                        reinterpret_cast<HWND>(self->winId()),
                        Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                            [self, url, renderFrames](HRESULT result, ICoreWebView2Controller *controller) -> HRESULT {
                                if (!self) return S_OK;
                                if (FAILED(result) || !controller) {
                                    emit self->errorOccurred("Could not create the browser source.");
                                    return S_OK;
                                }
                                self->impl_->controller = controller;
                                controller->get_CoreWebView2(&self->impl_->web);
                                auto &web = self->impl_->web;
                                RECT bounds{0, 0, self->width(), self->height()};
                                controller->put_Bounds(bounds);
                                controller->put_IsVisible(TRUE);
                                ComPtr<ICoreWebView2Controller2> transparent;
                                if (SUCCEEDED(controller->QueryInterface(IID_PPV_ARGS(&transparent))))
                                    transparent->put_DefaultBackgroundColor({0, 0, 0, 0});
                                EventRegistrationToken token{};
                                web->add_PermissionRequested(Callback<ICoreWebView2PermissionRequestedEventHandler>(
                                    [](ICoreWebView2*, ICoreWebView2PermissionRequestedEventArgs *args) -> HRESULT {
                                        // Receiving/overlay pages do not need local camera or microphone access.
                                        args->put_State(COREWEBVIEW2_PERMISSION_STATE_DENY);
                                        return S_OK;
                                    }).Get(), &token);
                                web->add_NavigationStarting(Callback<ICoreWebView2NavigationStartingEventHandler>(
                                    [](ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs *args) -> HRESULT {
                                        LPWSTR raw = nullptr;
                                        args->get_Uri(&raw);
                                        const QUrl target(QString::fromWCharArray(raw ? raw : L""));
                                        CoTaskMemFree(raw);
                                        if (target.scheme() != "https" && target.scheme() != "http") args->put_Cancel(TRUE);
                                        return S_OK;
                                    }).Get(), &token);
                                web->add_NewWindowRequested(Callback<ICoreWebView2NewWindowRequestedEventHandler>(
                                    [](ICoreWebView2*, ICoreWebView2NewWindowRequestedEventArgs *args) -> HRESULT {
                                        args->put_Handled(TRUE);
                                        BOOL user = FALSE;
                                        args->get_IsUserInitiated(&user);
                                        LPWSTR raw = nullptr;
                                        args->get_Uri(&raw);
                                        const QUrl target(QString::fromWCharArray(raw ? raw : L""));
                                        CoTaskMemFree(raw);
                                        if (user && (target.scheme() == "https" || target.scheme() == "http")) QDesktopServices::openUrl(target);
                                        return S_OK;
                                    }).Get(), &token);
                                ComPtr<ICoreWebView2_4> web4;
                                if (SUCCEEDED(web.As(&web4))) {
                                    web4->add_DownloadStarting(Callback<ICoreWebView2DownloadStartingEventHandler>(
                                        [](ICoreWebView2*, ICoreWebView2DownloadStartingEventArgs *args) -> HRESULT {
                                            args->put_Cancel(TRUE); return S_OK;
                                        }).Get(), &token);
                                }
                                web->add_ProcessFailed(Callback<ICoreWebView2ProcessFailedEventHandler>(
                                    [self](ICoreWebView2*, ICoreWebView2ProcessFailedEventArgs*) -> HRESULT {
                                        if (self) {
                                            self->impl_->timer.stop();
                                            emit self->errorOccurred("Browser process stopped. Remove and reopen this source to reconnect.");
                                        }
                                        return S_OK;
                                    }).Get(), &token);
                                web->add_NavigationCompleted(Callback<ICoreWebView2NavigationCompletedEventHandler>(
                                    [self](ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs *args) -> HRESULT {
                                        if (!self) return S_OK;
                                        BOOL success = FALSE;
                                        args->get_IsSuccess(&success);
                                        if (!success) emit self->errorOccurred("Browser source could not load. Check its URL and connection.");
                                        return S_OK;
                                    }).Get(), &token);
                                UINT32 pid = 0;
                                web->get_BrowserProcessId(&pid);
                                emit self->audioProcessReady(pid);
                                web->Navigate(url.toString(QUrl::FullyEncoded).toStdWString().c_str());
                                if (renderFrames) self->impl_->timer.start(67);
                                return S_OK;
                            }).Get());
                    if (FAILED(created)) emit self->errorOccurred("Could not initialize the browser source.");
                    return S_OK;
                }).Get());
        if (FAILED(hr)) emit self->errorOccurred("Install Microsoft Edge WebView2 Evergreen Runtime to use browser sources.");
#else
        emit self->errorOccurred("Browser sources currently require Windows and WebView2.");
#endif
    });
    connect(&impl_->timer, &QTimer::timeout, this, &BrowserSource::capture);
}

BrowserSource::~BrowserSource() {
    impl_->timer.stop();
#ifdef _WIN32
    if (impl_->controller) impl_->controller->Close();
#endif
}

void BrowserSource::park() { setAttribute(Qt::WA_ShowWithoutActivating, true); move(-16000, -16000); show(); }
void BrowserSource::interact() { setAttribute(Qt::WA_ShowWithoutActivating, false); move(100, 100); show(); raise(); activateWindow(); }
void BrowserSource::closeEvent(QCloseEvent *event) {
    if (qApp->property("video_mixer_quitting").toBool()) event->accept();
    else { event->ignore(); park(); }
}
void BrowserSource::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
#ifdef _WIN32
    if (impl_->controller) {
        RECT bounds{0, 0, width(), height()};
        impl_->controller->put_Bounds(bounds);
    }
#endif
}

void BrowserSource::capture() {
#ifdef _WIN32
    if (!impl_->web || impl_->busy) return;
    ComPtr<IStream> stream;
    if (FAILED(CreateStreamOnHGlobal(nullptr, TRUE, &stream))) return;
    impl_->busy = true;
    QPointer<BrowserSource> self(this);
    const HRESULT result = impl_->web->CapturePreview(COREWEBVIEW2_CAPTURE_PREVIEW_IMAGE_FORMAT_PNG,
        stream.Get(), Callback<ICoreWebView2CapturePreviewCompletedHandler>(
            [self, stream](HRESULT result) -> HRESULT {
                if (!self) return S_OK;
                self->impl_->busy = false;
                if (FAILED(result)) return S_OK;
                STATSTG stat{};
                if (FAILED(stream->Stat(&stat, STATFLAG_NONAME)) || stat.cbSize.QuadPart > 32 * 1024 * 1024) return S_OK;
                HGLOBAL buffer = nullptr;
                if (FAILED(GetHGlobalFromStream(stream.Get(), &buffer))) return S_OK;
                auto *data = static_cast<const uchar*>(GlobalLock(buffer));
                if (!data) return S_OK;
                const QImage image = QImage::fromData(data, static_cast<int>(stat.cbSize.QuadPart), "PNG");
                GlobalUnlock(buffer);
                if (!image.isNull()) emit self->frameReady(image);
                return S_OK;
            }).Get());
    if (FAILED(result)) impl_->busy = false;
#endif
}
} // namespace versus::mixer
