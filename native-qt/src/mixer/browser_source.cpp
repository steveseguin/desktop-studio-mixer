#include "versus/app/performance_trace.h"
#include "versus/mixer/browser_source.h"
#include "versus/mixer/audio_monitor.h"
#include "versus/mixer/frame_image.h"
#include "versus/video/window_capture.h"
#include "versus/mixer/process_worker.h"
#include <QThreadPool>
#include <QElapsedTimer>
#include <QCryptographicHash>
#include <QUuid>
#include <QtEndian>
#include <algorithm>
#include <QCloseEvent>
#include <QApplication>
#include <QDir>
#include <QPointer>
#include <QStandardPaths>
#include <QTimer>
#include <QShortcut>
#include <QUrl>
#include <mutex>
#include <future>
#include <thread>
#include <map>
#include <utility>
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
    struct Proxy {
        // Declared first so completion fires after the worker/job is destroyed.
        struct Finished { std::promise<void> promise; ~Finished() { promise.set_value(); } } finished;
        ProcessWorker worker;
        std::shared_future<void> previous;
        std::atomic<bool> ready{false}, failed{false}, cancelled{false};
        QString error;
        qint64 heartbeat = -1;
        QElapsedTimer seen;
        QElapsedTimer statusAt;
    };
    std::shared_ptr<Proxy> proxy;
    std::function<void()> startWorker;
    std::thread audioReader;
    bool restarting = false;
    bool interactionPending = false;
    QElapsedTimer messageClock;
    int messageCount = 0, pcmBytes = 0;
    QTimer timer;
    QTimer rendererHealth;
    QElapsedTimer rendererProbeAt;
    bool rendererProbePending = false;
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

BrowserSource::BrowserSource(const QString &id, const QUrl &url, bool renderFrames, bool transparent, bool allowTalkback, bool preserveColorMetadata)
    : QWidget(nullptr, renderFrames && !transparent ? Qt::Tool | Qt::FramelessWindowHint : Qt::Tool), impl_(std::make_unique<Impl>()) {
    setWindowTitle("Desktop Studio - Browser source");
    setAttribute(Qt::WA_QuitOnClose, false);
    resize(1280, 720);
    if (!ProcessWorker::inWorker()) {
        auto proxy = impl_->proxy = std::make_shared<Impl::Proxy>();
        static std::mutex profilesMutex;
        static std::map<QString, std::shared_future<void>> profiles;
        const auto profileKey = !QUuid(id).isNull() ? QUuid(id).toString(QUuid::WithoutBraces) : id;
        {
            std::lock_guard lock(profilesMutex);
            if (const auto previous = profiles.find(profileKey); previous != profiles.end()) proxy->previous = previous->second;
            for (auto it = profiles.begin(); it != profiles.end();) {
                if (it->second.wait_for(std::chrono::seconds(0)) == std::future_status::ready) it = profiles.erase(it);
                else ++it;
            }
            profiles[profileKey] = proxy->finished.promise.get_future().share();
        }
        impl_->startWorker = [proxy, id, url, renderFrames, transparent, allowTalkback, preserveColorMetadata] {
            if (!proxy->worker.start("browser")) { proxy->error = proxy->worker.error(); proxy->failed = true; return; }
            const auto result = proxy->worker.call({{"op", "start"}, {"id", id}, {"locator", url.toString(QUrl::FullyEncoded)},
                {"render", renderFrames}, {"transparent", transparent}, {"talkback", allowTalkback}, {"preserveColorMetadata", preserveColorMetadata}});
            if (proxy->cancelled) { proxy->worker.stop(); return; }
            if (!result["ok"].toBool()) { proxy->error = result["error"].toString(); proxy->failed = true; return; }
            proxy->ready = true;
        };
        connect(&impl_->timer, &QTimer::timeout, this, [this, proxy] {
            // A WebView profile must not be shared across two worker jobs. Wait
            // on the GUI timer rather than occupying a thread-pool thread that
            // the old worker's retirement might need.
            if (impl_->startWorker && (!proxy->previous.valid() ||
                proxy->previous.wait_for(std::chrono::seconds(0)) == std::future_status::ready))
                QThreadPool::globalInstance()->start(std::exchange(impl_->startWorker, {}));
            if (!proxy->ready && !proxy->failed) return;
            if (proxy->failed || !proxy->worker.alive()) {
                proxy->worker.terminate();
                impl_->timer.stop(); emit errorOccurred(proxy->failed && !proxy->error.isEmpty()
                    ? proxy->error + " Reconnecting automatically..." : "Browser worker stopped. Reconnecting automatically...");
                QTimer::singleShot(2000, this, [this] { emit restartRequired(); }); return;
            }
            if (impl_->interactionPending) {
                impl_->interactionPending = false;
                const auto title = windowTitle();
                QThreadPool::globalInstance()->start([proxy, title] {
                    if (!proxy->cancelled) proxy->worker.call({{"op", "interact"}, {"title", title}});
                });
            }
            QJsonObject state;
            if (!proxy->statusAt.isValid() || proxy->statusAt.elapsed() >= 100) {
                proxy->statusAt.restart(); state = proxy->worker.status();
            }
            if (!state.isEmpty()) {
                const auto heartbeat = state["heartbeat"].toInteger();
                if (heartbeat != proxy->heartbeat) { proxy->heartbeat = heartbeat; proxy->seen.restart(); }
                else if (proxy->seen.isValid() && proxy->seen.elapsed() > 10000) { proxy->worker.terminate(); return; }
                impl_->captureMode = state["description"].toString("Starting browser");
                const auto error = state["error"].toString();
                if (error != proxy->error) { proxy->error = error; if (error.isEmpty()) emit recovered(); else emit errorOccurred(error); }
            }
            const auto image = proxy->worker.image(true);
            if (!image.isNull()) emit frameReady(image);
            if (!impl_->audioReader.joinable()) impl_->audioReader = std::thread([this, proxy] {
                // Audio follows its media event, independent of GUI painting,
                // accessibility calls or a slow video frame copy.
                while (!proxy->cancelled && proxy->worker.alive()) {
                    audio::StreamChunk chunk;
                    if (proxy->worker.audio(chunk)) {
                        QByteArray pcm(qsizetype(chunk.samples.size()*2), Qt::Uninitialized);
                        for (size_t i=0; i<chunk.samples.size(); ++i) qToLittleEndian<qint16>(qint16(std::clamp(chunk.samples[i], -1.f, 1.f)*32767), pcm.data()+i*2);
                        emit audioReady(pcm, int(chunk.sampleRate));
                    }
                    proxy->worker.waitForMedia(20);
                }
            });
        });
        impl_->timer.setTimerType(Qt::PreciseTimer); impl_->timer.start(8);
        return;
    }
    auto *escape = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    connect(escape, &QShortcut::activated, this, &BrowserSource::park);
    park();
    QPointer<BrowserSource> self(this);
    QTimer::singleShot(0, this, [self, id, url, renderFrames, transparent, allowTalkback, preserveColorMetadata] {
        if (!self) return;
#ifdef _WIN32
        const auto safeId = !QUuid(id).isNull() ? QUuid(id).toString(QUuid::WithoutBraces) :
            QString::fromLatin1(QCryptographicHash::hash(id.toUtf8(), QCryptographicHash::Sha256).toHex());
        const auto profile = QDir(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation))
            .filePath("VideoMixer/browser/" + safeId);
        QDir().mkpath(profile);
        auto options = Microsoft::WRL::Make<CoreWebView2EnvironmentOptions>();
        options->put_AdditionalBrowserArguments(L"--autoplay-policy=no-user-gesture-required --disable-backgrounding-occluded-windows --disable-renderer-backgrounding");
        const HRESULT hr = CreateCoreWebView2EnvironmentWithOptions(nullptr,
            profile.toStdWString().c_str(), options.Get(),
            Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
                [self, url, renderFrames, transparent, allowTalkback, preserveColorMetadata](HRESULT result, ICoreWebView2Environment *environment) -> HRESULT {
                    if (!self) return S_OK;
                    if (FAILED(result) || !environment) {
                        emit self->errorOccurred("Browser sources need Microsoft Edge WebView2 Evergreen Runtime. Install it from microsoft.com/edge/webview2, then reopen the source.");
                        return S_OK;
                    }
                    const HRESULT created = environment->CreateCoreWebView2Controller(
                        reinterpret_cast<HWND>(self->winId()),
                        Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                            [self, url, renderFrames, transparent, allowTalkback, preserveColorMetadata](HRESULT result, ICoreWebView2Controller *controller) -> HRESULT {
                                if (!self) return S_OK;
                                if (FAILED(result) || !controller) {
                                    emit self->errorOccurred("Could not create the browser source.");
                                    return S_OK;
                                }
                                self->impl_->controller = controller;
                                controller->get_CoreWebView2(&self->impl_->web);
                                auto &web = self->impl_->web;
                                ComPtr<ICoreWebView2Settings> settings;
                                ComPtr<ICoreWebView2_27> web27;
                                ComPtr<ICoreWebView2_18> web18;
                                ComPtr<ICoreWebView2_4> web4;
                                // Do not silently lose permission/download protections on
                                // an old or disconnected runtime. Navigate only after securing it.
                                if (!web || FAILED(web->get_Settings(&settings)) ||
                                    FAILED(web.As(&web27)) || FAILED(web.As(&web18)) || FAILED(web.As(&web4)) ||
                                    FAILED(settings->put_AreHostObjectsAllowed(FALSE)) ||
                                    FAILED(settings->put_AreDevToolsEnabled(FALSE)) ||
                                    FAILED(settings->put_AreDefaultScriptDialogsEnabled(FALSE)) ||
                                    FAILED(settings->put_AreDefaultContextMenusEnabled(FALSE)) ||
                                    FAILED(settings->put_IsStatusBarEnabled(FALSE)) ||
                                    FAILED(settings->put_IsWebMessageEnabled(renderFrames))) {
                                    emit self->errorOccurred("Update Microsoft Edge WebView2 Evergreen Runtime to use browser sources securely.");
                                    controller->Close(); self->impl_->web.Reset(); self->impl_->controller.Reset();
                                    return S_OK;
                                }
                                RECT bounds{0, 0, self->width(), self->height()};
                                controller->put_Bounds(bounds);
                                controller->put_IsVisible(TRUE);
                                ComPtr<ICoreWebView2Controller2> backgroundController;
                                if (SUCCEEDED(controller->QueryInterface(IID_PPV_ARGS(&backgroundController))))
                                    backgroundController->put_DefaultBackgroundColor({0, 0, 0, 0});
                                const auto secure = [&](HRESULT registered) {
                                    if (SUCCEEDED(registered)) return true;
                                    emit self->errorOccurred("Browser security initialization failed. Reconnect this source or update WebView2.");
                                    controller->Close(); self->impl_->web.Reset(); self->impl_->controller.Reset();
                                    return false;
                                };
                                EventRegistrationToken token{};
                                if (!secure(web27->add_ScreenCaptureStarting(
                                    Callback<ICoreWebView2ScreenCaptureStartingEventHandler>(
                                        [](ICoreWebView2*, ICoreWebView2ScreenCaptureStartingEventArgs *args) -> HRESULT {
                                            args->put_Cancel(TRUE); args->put_Handled(TRUE); return S_OK;
                                        }).Get(), &token))) return S_OK;
                                if (!secure(web18->add_LaunchingExternalUriScheme(
                                    Callback<ICoreWebView2LaunchingExternalUriSchemeEventHandler>(
                                        [](ICoreWebView2*, ICoreWebView2LaunchingExternalUriSchemeEventArgs *args) -> HRESULT { args->put_Cancel(TRUE); return S_OK; }).Get(), &token))) return S_OK;
                                if (!secure(web->add_FrameNavigationStarting(Callback<ICoreWebView2NavigationStartingEventHandler>(
                                    [](ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs *args) -> HRESULT {
                                        LPWSTR raw = nullptr; args->get_Uri(&raw);
                                        const QUrl target(QString::fromWCharArray(raw ? raw : L"")); CoTaskMemFree(raw);
                                        const auto scheme = target.scheme();
                                        if (scheme != "https" && scheme != "http" && scheme != "about" && scheme != "blob" && scheme != "data") args->put_Cancel(TRUE);
                                        return S_OK;
                                    }).Get(), &token))) return S_OK;
                                if (!secure(web->add_PermissionRequested(Callback<ICoreWebView2PermissionRequestedEventHandler>(
                                    [allowTalkback](ICoreWebView2*, ICoreWebView2PermissionRequestedEventArgs *args) -> HRESULT {
                                        COREWEBVIEW2_PERMISSION_KIND kind{}; args->get_PermissionKind(&kind);
                                        LPWSTR raw = nullptr; args->get_Uri(&raw);
                                        const QUrl origin(QString::fromWCharArray(raw ? raw : L"")); CoTaskMemFree(raw);
                                        // Only the explicitly opened talkback panel may prompt for a mic.
                                        const bool mic = allowTalkback && kind == COREWEBVIEW2_PERMISSION_KIND_MICROPHONE &&
                                            origin.scheme() == "https" && origin.host() == "vdo.ninja";
                                        args->put_State(mic ? COREWEBVIEW2_PERMISSION_STATE_DEFAULT : COREWEBVIEW2_PERMISSION_STATE_DENY);
                                        return S_OK;
                                    }).Get(), &token))) return S_OK;
                                if (!secure(web->add_NavigationStarting(Callback<ICoreWebView2NavigationStartingEventHandler>(
                                    [](ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs *args) -> HRESULT {
                                        LPWSTR raw = nullptr;
                                        args->get_Uri(&raw);
                                        const QUrl target(QString::fromWCharArray(raw ? raw : L""));
                                        CoTaskMemFree(raw);
                                        if (target.scheme() != "https" && target.scheme() != "http") args->put_Cancel(TRUE);
                                        return S_OK;
                                    }).Get(), &token))) return S_OK;
                                if (!secure(web->add_NewWindowRequested(Callback<ICoreWebView2NewWindowRequestedEventHandler>(
                                    [](ICoreWebView2*, ICoreWebView2NewWindowRequestedEventArgs *args) -> HRESULT {
                                        args->put_Handled(TRUE);
                                        // A source cannot launch the system browser or another application.
                                        return S_OK;
                                    }).Get(), &token))) return S_OK;
                                {
                                    if (!secure(web4->add_DownloadStarting(Callback<ICoreWebView2DownloadStartingEventHandler>(
                                        [](ICoreWebView2*, ICoreWebView2DownloadStartingEventArgs *args) -> HRESULT {
                                            args->put_Cancel(TRUE); return S_OK;
                                        }).Get(), &token))) return S_OK;
                                }
                                web->add_ProcessFailed(Callback<ICoreWebView2ProcessFailedEventHandler>(
                                    [self](ICoreWebView2*, ICoreWebView2ProcessFailedEventArgs*) -> HRESULT {
                                        if (self && !self->impl_->restarting) {
                                            self->impl_->restarting = true;
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
                                        [self](ICoreWebView2 *sender, ICoreWebView2WebMessageReceivedEventArgs *args) -> HRESULT {
                                            app::detail::PerformanceTrace::Scope profile(app::detail::PerformanceTrace::BrowserMessage);
                                            if (!self) return S_OK;
                                            auto &state = *self->impl_;
                                            if (!state.messageClock.isValid() || state.messageClock.elapsed() >= 1000) {
                                                state.messageClock.restart(); state.messageCount = 0; state.pcmBytes = 0;
                                            }
                                            if (++state.messageCount > 200) return S_OK;
                                            LPWSTR origin = nullptr, current = nullptr;
                                            args->get_Source(&origin); sender->get_Source(&current);
                                            const QUrl source(QString::fromWCharArray(origin ? origin : L""));
                                            const QUrl page(QString::fromWCharArray(current ? current : L""));
                                            CoTaskMemFree(origin); CoTaskMemFree(current);
                                            if (source != page || (page.scheme() != "https" && page.scheme() != "http")) return S_OK;
                                            LPWSTR raw = nullptr; args->get_WebMessageAsJson(&raw);
                                            const size_t length = raw ? wcsnlen_s(raw, 40001) : 0;
                                            if (!length || length > 40000) { CoTaskMemFree(raw); return S_OK; }
                                            const QString json = QString::fromWCharArray(raw, int(length)); CoTaskMemFree(raw);
                                            const auto obj = QJsonDocument::fromJson(json.toUtf8()).object();
                                            if (obj["type"].toString() == "mixer-audio-error") {
                                                if (state.messageCount == 1) emit self->errorOccurred("Browser audio could not be routed. Check its audio meter or use another source URL.");
                                                return S_OK;
                                            }
                                            if (obj["type"].toString() != "mixer-audio") return S_OK;
                                            const int rate = obj["rate"].toInt();
                                            if (rate < 8000 || rate > 192000) return S_OK;
                                            const auto pcm = QByteArray::fromBase64(obj["pcm"].toString().toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
                                            if (pcm.isEmpty() || pcm.size() > 16384 || pcm.size()%4) return S_OK;
                                            state.pcmBytes += int(pcm.size());
                                            if (state.pcmBytes > 768000) return S_OK;
                                            emit self->audioReady(pcm, rate);
                                            return S_OK;
                                        }).Get(), &token);
                                    QFile script(":/mixer/mixer-audio.js");
                                    QFile overlay(":/mixer/mixer-overlay.js");
                                    QFile webrtc(":/mixer/mixer-webrtc.js");
                                    if (!script.open(QIODevice::ReadOnly) || (transparent && !overlay.open(QIODevice::ReadOnly)) ||
                                        (!preserveColorMetadata && !webrtc.open(QIODevice::ReadOnly))) {
                                        emit self->errorOccurred("Browser source initialization is missing. Reinstall Desktop Studio.");
                                        return S_OK;
                                    }
                                    {
                                        const auto javascript = (QString::fromUtf8(script.readAll()) + "\n" +
                                            (transparent ? QString::fromUtf8(overlay.readAll()) : QString{}) + "\n" +
                                            (!preserveColorMetadata ? QString::fromUtf8(webrtc.readAll()) : QString{})).toStdWString();
                                        if (!secure(web->AddScriptToExecuteOnDocumentCreated(javascript.c_str(),
                                            Callback<ICoreWebView2AddScriptToExecuteOnDocumentCreatedCompletedHandler>(
                                                [self, url](HRESULT result, LPCWSTR) -> HRESULT {
                                                    if (!self || !self->impl_->web) return S_OK;
                                                    if (FAILED(result)) {
                                                        emit self->errorOccurred("Browser source initialization failed. Reconnect this source.");
                                                        return S_OK;
                                                    }
                                                    self->impl_->web->Navigate(url.toString(QUrl::FullyEncoded).toStdWString().c_str());
                                                    return S_OK;
                                                }).Get()))) return S_OK;
                                    }
                                } else {
                                    web->Navigate(url.toString(QUrl::FullyEncoded).toStdWString().c_str());
                                }
                                if (renderFrames && !transparent) {
                                    auto *state = self->impl_.get();
                                    state->capture = std::make_unique<video::WindowCapture>();
                                    state->capture->setFrameCallback([state, self](video::CapturedFrame frame) {
                                        // This path is explicitly opaque. Transparency uses PNG
                                        // snapshots below; do not premultiply/blend every WGC frame.
                                        frame.opaque = true;
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
#ifdef _WIN32
    // A worker heartbeat does not prove the page's renderer is responsive:
    // JavaScript can spin forever while the native host keeps processing events.
    connect(&impl_->rendererHealth, &QTimer::timeout, this, [this, self] {
        if (!impl_->web || impl_->restarting) return;
        if (impl_->rendererProbePending) {
            if (impl_->rendererProbeAt.elapsed() > 10000) {
                impl_->restarting = true; impl_->timer.stop();
                emit errorOccurred("Browser page stopped responding. Reconnecting automatically...");
                emit restartRequired();
            }
            return;
        }
        impl_->rendererProbePending = true; impl_->rendererProbeAt.restart();
        const auto result = impl_->web->ExecuteScript(L"0",
            Callback<ICoreWebView2ExecuteScriptCompletedHandler>([self](HRESULT, LPCWSTR) -> HRESULT {
                if (self) self->impl_->rendererProbePending = false;
                return S_OK;
            }).Get());
        if (FAILED(result)) impl_->rendererProbePending = false;
    });
    impl_->rendererHealth.start(2000);
#endif
}

BrowserSource::~BrowserSource() {
    impl_->timer.stop();
    if (auto proxy = std::move(impl_->proxy)) {
        proxy->cancelled = true;
        if (impl_->audioReader.joinable()) impl_->audioReader.join();
        QThreadPool::globalInstance()->start([proxy] { if (proxy->ready || proxy->failed) proxy->worker.stop(); });
        return;
    }
    unregisterBrowserPlayback(impl_->mutedBrowserPid);
    if (impl_->capture) impl_->capture->stopCapture();
#ifdef _WIN32
    if (impl_->controller) impl_->controller->Close();
#endif
}
QString BrowserSource::captureDescription() const { return impl_->captureMode; }

void BrowserSource::park() {
    if (auto proxy = impl_->proxy) { impl_->interactionPending = false; if (proxy->ready) QThreadPool::globalInstance()->start([proxy] { proxy->worker.call({{"op", "park"}}); }); return; }
    setAttribute(Qt::WA_ShowWithoutActivating, true); move(-16000, -16000); show();
}
void BrowserSource::interact() {
    // Chat/talkback panels request interaction immediately after construction,
    // before the asynchronous host is ready. Deliver it on the first ready tick.
    if (impl_->proxy) { impl_->interactionPending = true; return; }
    setAttribute(Qt::WA_ShowWithoutActivating, false); move(100, 100); show(); raise(); activateWindow();
}
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
