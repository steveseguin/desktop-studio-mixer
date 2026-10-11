#include "versus/app/performance_trace.h"
#include "versus/mixer/browser_source.h"
#include "versus/mixer/audio_monitor.h"
#include "versus/mixer/frame_image.h"
#include "versus/video/window_capture.h"
#include <QCloseEvent>
#include <QApplication>
#include <QDesktopServices>
#include <QDir>
#include <QPointer>
#include <QStandardPaths>
#include <QTimer>
#include <QShortcut>
#include <QUrl>
#include <mutex>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
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
    unsigned long mutedBrowserPid = 0;
    bool busy = false;
    bool deliveryPending = false;
    int failures = 0;
    QString captureMode = "Starting browser";
    std::unique_ptr<video::WindowCapture> capture;
    std::mutex frameMutex;
    QImage latest;
    QByteArray snapshotPng;
    QImage snapshotImage;
#ifdef _WIN32
    ComPtr<ICoreWebView2Controller> controller;
    ComPtr<ICoreWebView2> web;
#endif
};

BrowserSource::BrowserSource(const QString &id, const QUrl &url, bool renderFrames, bool transparent, bool allowTalkback)
    : QWidget(nullptr, renderFrames && !transparent ? Qt::Tool | Qt::FramelessWindowHint : Qt::Tool), impl_(std::make_unique<Impl>()) {
    setWindowTitle("Desktop Studio - Browser source");
    setAttribute(Qt::WA_QuitOnClose, false);
    resize(1280, 720);
    auto *escape = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    connect(escape, &QShortcut::activated, this, &BrowserSource::park);
    park();
    QPointer<BrowserSource> self(this);
    QTimer::singleShot(0, this, [self, id, url, renderFrames, transparent, allowTalkback] {
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
                [self, url, renderFrames, transparent, allowTalkback](HRESULT result, ICoreWebView2Environment *environment) -> HRESULT {
                    if (!self) return S_OK;
                    if (FAILED(result) || !environment) {
                        emit self->errorOccurred("Browser sources need Microsoft Edge WebView2 Evergreen Runtime. Install it from microsoft.com/edge/webview2, then reopen the source.");
                        return S_OK;
                    }
                    const HRESULT created = environment->CreateCoreWebView2Controller(
                        reinterpret_cast<HWND>(self->winId()),
                        Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                            [self, url, renderFrames, transparent, allowTalkback](HRESULT result, ICoreWebView2Controller *controller) -> HRESULT {
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
                                ComPtr<ICoreWebView2Controller2> backgroundController;
                                if (SUCCEEDED(controller->QueryInterface(IID_PPV_ARGS(&backgroundController))))
                                    backgroundController->put_DefaultBackgroundColor({0, 0, 0, 0});
                                EventRegistrationToken token{};
                                web->add_PermissionRequested(Callback<ICoreWebView2PermissionRequestedEventHandler>(
                                    [allowTalkback](ICoreWebView2*, ICoreWebView2PermissionRequestedEventArgs *args) -> HRESULT {
                                        COREWEBVIEW2_PERMISSION_KIND kind{}; args->get_PermissionKind(&kind);
                                        LPWSTR raw = nullptr; args->get_Uri(&raw);
                                        const QUrl origin(QString::fromWCharArray(raw ? raw : L"")); CoTaskMemFree(raw);
                                        // Only the explicitly opened talkback panel may prompt for a mic.
                                        const bool mic = allowTalkback && kind == COREWEBVIEW2_PERMISSION_KIND_MICROPHONE &&
                                            origin.scheme() == "https" && origin.host() == "vdo.ninja";
                                        args->put_State(mic ? COREWEBVIEW2_PERMISSION_STATE_DEFAULT : COREWEBVIEW2_PERMISSION_STATE_DENY);
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
                                            emit self->errorOccurred("Browser process stopped. Reconnecting automatically...");
                                            QTimer::singleShot(2000, self, [self] { if (self) emit self->restartRequired(); });
                                        }
                                        return S_OK;
                                    }).Get(), &token);
                                web->add_NavigationCompleted(Callback<ICoreWebView2NavigationCompletedEventHandler>(
                                    [self](ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs *args) -> HRESULT {
                                        if (!self) return S_OK;
                                        BOOL success = FALSE;
                                        args->get_IsSuccess(&success);
                                        if (!success) {
                                            emit self->errorOccurred("Browser source could not load. Retrying automatically...");
                                            const int retry = std::min(30000, 1000 << std::min(5, self->impl_->failures++));
                                            QTimer::singleShot(retry, self, [self] { if (self && self->impl_->web) self->impl_->web->Reload(); });
                                        } else { self->impl_->failures = 0; emit self->recovered(); }
                                        return S_OK;
                                    }).Get(), &token);
                                UINT32 pid = 0;
                                web->get_BrowserProcessId(&pid);
                                if (renderFrames) {
                                    self->impl_->mutedBrowserPid = pid;
                                    registerBrowserPlayback(pid);
                                    web->add_WebMessageReceived(Callback<ICoreWebView2WebMessageReceivedEventHandler>(
                                        [self](ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs *args) -> HRESULT {
                                            if (!self) return S_OK;
                                            LPWSTR raw = nullptr; args->get_WebMessageAsJson(&raw);
                                            const QString json = QString::fromWCharArray(raw ? raw : L""); CoTaskMemFree(raw);
                                            if (json.size() > 40000) return S_OK;
                                            const auto obj = QJsonDocument::fromJson(json.toUtf8()).object();
                                            if (obj["type"].toString() == "mixer-audio-error") {
                                                emit self->errorOccurred("Browser audio could not be routed. Check its audio meter or use another source URL.");
                                                return S_OK;
                                            }
                                            if (obj["type"].toString() != "mixer-audio") return S_OK;
                                            const int rate = obj["rate"].toInt();
                                            if (rate < 8000 || rate > 192000) return S_OK;
                                            const auto pcm = QByteArray::fromBase64(obj["pcm"].toString().toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
                                            if (pcm.isEmpty() || pcm.size() > 16384 || pcm.size()%4) return S_OK;
                                            emit self->audioReady(pcm, rate);
                                            return S_OK;
                                        }).Get(), &token);
                                    QFile script(":/mixer/mixer-audio.js");
                                    if (script.open(QIODevice::ReadOnly)) {
                                        const auto javascript = QString::fromUtf8(script.readAll()).toStdWString();
                                        web->AddScriptToExecuteOnDocumentCreated(javascript.c_str(),
                                            Callback<ICoreWebView2AddScriptToExecuteOnDocumentCreatedCompletedHandler>(
                                                [self, url](HRESULT result, LPCWSTR) -> HRESULT {
                                                    if (!self) return S_OK;
                                                    if (FAILED(result)) emit self->errorOccurred("Browser audio initialization failed. Reconnect this source.");
                                                    self->impl_->web->Navigate(url.toString(QUrl::FullyEncoded).toStdWString().c_str());
                                                    return S_OK;
                                                }).Get());
                                    }
                                } else {
                                    web->Navigate(url.toString(QUrl::FullyEncoded).toStdWString().c_str());
                                }
                                if (renderFrames && !transparent) {
                                    auto *state = self->impl_.get();
                                    state->capture = std::make_unique<video::WindowCapture>();
                                    state->capture->setFrameCallback([state, self](video::CapturedFrame frame) {
                                        auto image = takeFrameImage(std::move(frame)); if (image.isNull()) return;
                                        std::lock_guard lock(state->frameMutex); state->latest = std::move(image);
                                        if (!state->deliveryPending && self) {
                                            state->deliveryPending = true;
                                            QMetaObject::invokeMethod(self, [self] { if (self) self->capture(); }, Qt::QueuedConnection);
                                        }
                                    });
                                    if (state->capture->startCapture(std::to_string(self->winId()), 1280, 720, 30)) {
                                        state->captureMode = "Windows capture / 30 fps";
                                        // Delivery follows capture, coalescing under load instead of sampling through a second timer.
                                    } else state->capture.reset();
                                }
                                if (renderFrames && !self->impl_->capture) {
                                    self->impl_->captureMode = "Transparent snapshots / 15 fps";
                                    self->impl_->timer.start(67);
                                }
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
    unregisterBrowserPlayback(impl_->mutedBrowserPid);
    if (impl_->capture) impl_->capture->stopCapture();
#ifdef _WIN32
    if (impl_->controller) impl_->controller->Close();
#endif
}
QString BrowserSource::captureDescription() const { return impl_->captureMode; }

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
    if (impl_->capture) {
        QImage image;
        { std::lock_guard lock(impl_->frameMutex); image.swap(impl_->latest); impl_->deliveryPending = false; }
        if (!image.isNull()) emit frameReady(image);
        return;
    }
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
                auto &state = *self->impl_;
                const auto bytes = QByteArray::fromRawData(reinterpret_cast<const char*>(data), static_cast<int>(stat.cbSize.QuadPart));
                // WebView still samples at the normal cadence. A static overlay
                // can reuse its decoded pixels without hiding later changes.
                if (state.snapshotImage.isNull() || bytes != state.snapshotPng) {
                    app::detail::PerformanceTrace::Scope profile(app::detail::PerformanceTrace::BrowserDecode);
                    const auto image = QImage::fromData(data, static_cast<int>(stat.cbSize.QuadPart), "PNG");
                    if (!image.isNull()) {
                        state.snapshotPng = QByteArray(bytes.constData(), bytes.size());
                        state.snapshotImage = image;
                    }
                }
                GlobalUnlock(buffer);
                if (!state.snapshotImage.isNull()) emit self->frameReady(state.snapshotImage);
                return S_OK;
            }).Get());
    if (FAILED(result)) impl_->busy = false;
#endif
}
} // namespace versus::mixer
