#include "versus/mixer/mixer_widget.h"
#include "versus/mixer/browser_source.h"
#include "versus/mixer/audio_monitor.h"
#include "versus/mixer/program_recorder.h"
#include "versus/mixer/isolated_source.h"
#include "versus/mixer/gpu_compositor.h"
#include "versus/mixer/local_outputs.h"
#include "versus/mixer/audio_processor.h"
#include "versus/mixer/guest_return.h"
#include <QThreadPool>
#include "versus/mixer/audio_level_meter.h"
#include "versus/app/versus_app.h"
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QColorDialog>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QImageReader>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPointer>
#include <QPushButton>
#include <QSaveFile>
#include <QSettings>
#include "versus/app/performance_trace.h"
#include <QSignalBlocker>
#include <QScrollArea>
#include <QShortcut>
#include <QSlider>
#include <QSpinBox>
#include <QSplitter>
#include <QStandardPaths>
#include <QTimer>
#include <QUrlQuery>
#include <QUuid>
#include <algorithm>
#include <array>
#include "versus/mixer/frame_image.h"
#include <cmath>
#include <climits>
#include <deque>
#include <mutex>
#include <thread>
#include <QtEndian>

namespace versus::mixer {
namespace {
using Profile = app::detail::PerformanceTrace;
constexpr int Width = 1280, Height = 720;
QString newId() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
int64_t now100ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count() / 100;
}
struct Layer {
    QString source;
    QRectF rect{0, 0, 1, 1};
    bool visible = true;
    double opacity = 1;
    bool locked = false;
    QRectF crop{0, 0, 1, 1};
    QString fit = "fit";
    bool operator==(const Layer &) const = default;
};
struct Scene {
    QString id = newId(), name;
    QColor background{"#101722"};
    std::vector<Layer> layers;
    bool operator==(const Scene &) const = default;
};
struct Source {
    QString id = newId(), name, kind, locator, invite, error;
    QColor color{"#183657"};
    std::mutex imageMutex, audioMutex;
    QImage image;
    bool imageStraightAlpha = false; // Protected by imageMutex, alongside image.
    std::deque<float> samples;
    std::deque<float> delayedSamples;
    std::atomic<int> audioDelayMs{0};
    int appliedDelayMs = -1;
    audio::AudioResamplerState resampler;
    std::atomic<float> gain{1}, peak{0};
    std::atomic<bool> noiseSuppression{false}, noiseGate{false}, compression{false};
    AudioProcessor processor;
    std::atomic<bool> muted{false}, followsScene{true}, onProgram{false};
    std::atomic<bool> monitored{true};
    std::atomic<bool> audioActive{false};
    bool transparent = false;
    bool mediaLoop = true, mediaPaused = false;
    bool starting = false;
    int retryAttempts = 0;
    int64_t retryAt = 0;
    QString windowTitle, windowExecutable, returnId;
    std::atomic<bool> admitted{true};
    std::atomic<std::shared_ptr<GuestReturn>> guestReturn;
    std::shared_ptr<std::atomic<bool>> captureValid = std::make_shared<std::atomic<bool>>(true);
    std::shared_future<void> cleanup;
    std::unique_ptr<IsolatedSource> media;
    int cameraWidth = Width, cameraHeight = Height;
    double cameraFps = 30;
    QJsonObject cameraControls;
    unsigned cameraGeneration = 0;
    std::atomic<int64_t> lastFrame{0};
    std::atomic<unsigned> frames{0};
    std::unique_ptr<IsolatedSource> window;
    std::unique_ptr<IsolatedSource> camera;
    std::unique_ptr<IsolatedSource> spout;
    std::unique_ptr<IsolatedSource> audio;
    QPointer<BrowserSource> browser;
    ~Source() {
        if (window) window->stopCapture();
        if (camera) camera->stopCapture();
        if (spout) spout->stopCapture();
        if (audio) audio->StopCapture();
        delete browser.data();
    }
    bool hasAudio() const {
        return kind == "window" || kind == "microphone" || kind == "desktop" ||
            kind == "browser" || kind == "guest" || kind == "wireless" || kind == "media";
    }
    bool hasVideo() const { return kind != "microphone" && kind != "desktop" && (kind != "media" || !media || media->hasVideo()); }
};

class Canvas : public QWidget {
public:
    QImage image;
    QRectF selection;
    std::function<void(QPointF)> pressed;
    std::function<void(QPointF)> dragged;
    std::function<void()> released;
    explicit Canvas(QWidget *parent = nullptr) : QWidget(parent) {
        setMinimumSize(240, 135);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    }
    void present(const QImage &frame, const QRectF &selected = {}) {
        if (image.cacheKey() == frame.cacheKey() && selection == selected) return;
        image = frame; selection = selected; update();
    }
    QRectF viewport() const {
        QSizeF size(image.isNull() ? QSize(Width, Height) : image.size()); size.scale(this->size(), Qt::KeepAspectRatio);
        return {(width() - size.width()) / 2, (height() - size.height()) / 2, size.width(), size.height()};
    }
    QPointF relative(QPointF point) const {
        const auto view = viewport();
        return {(point.x() - view.x()) / view.width(), (point.y() - view.y()) / view.height()};
    }
protected:
    void paintEvent(QPaintEvent*) override {
        Profile::Scope profile(Profile::CanvasPaint);
        QPainter painter(this);
        painter.fillRect(rect(), QColor("#080b11"));
        painter.drawImage(viewport(), image);
        if (!selection.isEmpty()) {
            const auto view = viewport();
            painter.setPen(QPen(QColor("#7ca8ff"), 2));
            const QRectF rect(view.x() + selection.x()*view.width(), view.y() + selection.y()*view.height(),
                selection.width()*view.width(), selection.height()*view.height());
            painter.drawRect(rect); painter.setBrush(QColor("#7ca8ff"));
            for (const auto &point : {rect.topLeft(), rect.topRight(), rect.bottomLeft(), rect.bottomRight()})
                painter.drawRect(QRectF(point.x()-4, point.y()-4, 8, 8));
        }
    }
    void mousePressEvent(QMouseEvent *event) override {
        if (event->button() == Qt::LeftButton && pressed) pressed(relative(event->position()));
    }
    void mouseReleaseEvent(QMouseEvent *event) override { if (event->button() == Qt::LeftButton && released) released(); }
    void mouseMoveEvent(QMouseEvent *event) override {
        if ((event->buttons() & Qt::LeftButton) && dragged) dragged(relative(event->position()));
    }
};
QPushButton *button(const QString &text, const QString &name, QBoxLayout *layout, QWidget *parent) {
    auto *result = new QPushButton(text, parent);
    result->setObjectName(name); result->setAccessibleName(text);
    layout->addWidget(result); return result;
}
bool webUrl(const QString &value) {
    const QUrl url(value, QUrl::StrictMode);
    return url.isValid() && !url.host().isEmpty() && (url.scheme() == "http" || url.scheme() == "https");
}
} // namespace

struct MixerWidget::Impl {
    MixerWidget *q;
    app::VersusApp *core;
    int Width = 1280, Height = 720, canvasFps = 30;
    int64_t nextRender = 0;
    ProgramRecorder recorder;
    LocalOutputs localOutputs;
    QCheckBox *spoutOutput = nullptr, *virtualCamera = nullptr, *guestReturnEnabled = nullptr;
    std::atomic<bool> privateTalkback{false};
    std::array<GpuCompositor,3> gpu;
    QComboBox *renderer = nullptr;
    QLabel *recordingStatus = nullptr;
    QComboBox *canvasFormat = nullptr;
    QCheckBox *layerLock = nullptr, *snap = nullptr;
    QComboBox *fitMode = nullptr;
    std::array<QDoubleSpinBox*, 4> cropControls{};
    int resizeCorner = -1;
    struct EditState { std::vector<Scene> scenes; std::vector<std::shared_ptr<Source>> sources; int selectedScene; QString selectedSource; };
    std::vector<EditState> history;
    size_t historyIndex = 0;
    bool restoring = false;
    QPushButton *undoButton = nullptr, *redoButton = nullptr;
    QTimer videoTimer, saveTimer;
    QElapsedTimer transition;
    bool fading = false, rebuilding = false, loading = false;
    bool studioMode = QSettings("VDO.Ninja", "Video Mixer").value("mixer/studioMode", false).toBool();
    std::mutex sourcesMutex;
    std::vector<std::shared_ptr<Source>> sources;
    std::vector<Scene> scenes;
    Scene program, previous;
    int selectedScene = 0;
    QString selectedSource;
    QPointF dragStart;
    QRectF dragRect;
    bool draggingLayer = false;
    struct CompositionKey {
        qint64 frame;
        QRectF target;
        double opacity;
        bool straightAlpha;
        QRectF crop;
        bool operator==(const CompositionKey &) const = default;
    };
    struct CompositionCache {
        QColor background;
        std::vector<CompositionKey> layers;
        QImage image;
    };
    // Bounded to Preview, Program and the outgoing transition scene.
    std::array<CompositionCache, 3> compositions;
    QListWidget *sceneList = nullptr, *sourceList = nullptr;
    Canvas *previewView = nullptr, *programView = nullptr;
    QPointer<Canvas> outputView;
    QWidget *programPanel = nullptr;
    QGroupBox *sourceBox = nullptr, *audioBox = nullptr;
    QHBoxLayout *monitors = nullptr;
    QSplitter *panels = nullptr;
    QLabel *brand = nullptr;
    QPushButton *takeButton = nullptr, *fadeButton = nullptr;
    QPointer<BrowserSource> chatDock;
    QPointer<BrowserSource> talkback;
    QString room = "mix"+newId().remove('-').left(24), roomPassword = newId().remove('-');
    QLabel *sourceHealth = nullptr, *outputStatus = nullptr;
    QPushButton *startOutput = nullptr, *recordOutput = nullptr, *cameraSettingsButton = nullptr;
    QCheckBox *monitorEnable = nullptr;
    QComboBox *monitorDevice = nullptr;
    std::atomic<bool> monitoring{false};
    std::atomic<int> monitorDeviceId{-1};
    std::atomic<float> monitorGain{0.5f};
    QElapsedTimer healthClock;
    unsigned lastFrameCount = 0;
    QString healthSource;
    QLabel *status = nullptr, *previewTitle = nullptr, *programTitle = nullptr;
    QVBoxLayout *audioRows = nullptr;
    std::vector<std::pair<std::weak_ptr<Source>, AudioLevelMeter*>> meters;
    AudioLevelMeter *masterMeter = nullptr;
    QDoubleSpinBox *x = nullptr, *y = nullptr, *w = nullptr, *h = nullptr;
    QSlider *opacity = nullptr;
    std::atomic<bool> running{true};
    std::atomic<float> masterGain{1}, masterPeak{0};
    std::thread audioThread;
    QString projectPath;

    Impl(MixerWidget *widget, app::VersusApp *appCore) : q(widget), core(appCore) {
        projectPath = QDir(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation))
            .filePath("VideoMixer/studio.mixer.json");
        buildUi();
        if (!load(projectPath) && QFileInfo::exists(projectPath)) {
            // Preserve a damaged/unrecognized show before autosave creates a fresh one.
            const auto backup = projectPath+".recovery-"+newId();
            if (!QFile::copy(projectPath, backup)) projectPath += ".recovered.mixer.json";
            message("The previous show could not be loaded. Its original file has been preserved.");
        }
        if (scenes.empty()) {
            auto slate = std::make_shared<Source>();
            slate->name = "Starting soon"; slate->kind = "title"; slate->locator = "Starting soon";
            sources.push_back(slate); startSource(slate);
            Scene opening; opening.name = "Opening"; opening.layers.push_back({slate->id}); scenes.push_back(opening);
            Scene main; main.name = "Main"; scenes.push_back(main);
            auto breakTitle = std::make_shared<Source>();
            breakTitle->name = "Be right back"; breakTitle->kind = "title"; breakTitle->locator = "Be right back";
            sources.push_back(breakTitle); startSource(breakTitle);
            Scene pause; pause.name = "Break"; pause.layers.push_back({breakTitle->id}); scenes.push_back(pause);
        }
        selectedScene = 0; program = scenes[0];
        rebuild(); updateAudioRouting(); updateModeUi();
        saveTimer.setSingleShot(true);
        QObject::connect(&saveTimer, &QTimer::timeout, q, [this] { save(projectPath); });
        videoTimer.setTimerType(Qt::PreciseTimer);
        QObject::connect(&videoTimer, &QTimer::timeout, q, [this] { render(); });
        videoTimer.start(1000/canvasFps);
        remember();
        healthClock.start();
        audioThread = std::thread([this] { mixAudio(); });
    }
    ~Impl() {
        shutdown();
    }
    void shutdown() {
        if (!running.exchange(false)) return;
        videoTimer.stop(); saveTimer.stop(); save(projectPath);
        if (audioThread.joinable()) audioThread.join();
        recorder.stop();
        for (const auto &item : sources) { if (auto output = item->guestReturn.exchange({})) QThreadPool::globalInstance()->start([output] { output->stop(); }); stopSource(item); }
        delete outputView.data();
        delete chatDock.data();
        delete talkback.data();
        sources.clear();
    }
    Scene &preview() { return scenes[static_cast<size_t>(selectedScene)]; }
    std::shared_ptr<Source> source(const QString &id) {
        for (const auto &item : sources) if (item->id == id) return item;
        return {};
    }
    Layer *selectedLayer() {
        for (auto &layer : preview().layers) if (layer.source == selectedSource) return &layer;
        return nullptr;
    }
    void changed() {
        if (loading) return;
        if (!studioMode && !scenes.empty()) {
            program = preview(); fading = false;
            updateAudioRouting();
        }
        saveTimer.start(500);
        if (!draggingLayer) remember();
    }
    void remember() {
        if (loading || restoring || scenes.empty()) return;
        if (!history.empty() && history[historyIndex].scenes == scenes && history[historyIndex].sources == sources) return;
        if (!history.empty()) history.resize(historyIndex+1);
        history.push_back({scenes, sources, selectedScene, selectedSource});
        if (history.size() > 65) history.erase(history.begin());
        historyIndex = history.size()-1;
        if (undoButton) undoButton->setEnabled(historyIndex > 0);
        if (redoButton) redoButton->setEnabled(false);
    }
    void undo(bool redo) {
        if (history.empty() || (!redo && historyIndex == 0) || (redo && historyIndex+1 >= history.size())) return;
        historyIndex = redo ? historyIndex+1 : historyIndex-1;
        const auto state = history[historyIndex]; restoring = true;
        for (const auto &item : sources) if (std::find(state.sources.begin(), state.sources.end(), item) == state.sources.end()) {
            if (auto output = item->guestReturn.exchange({})) QThreadPool::globalInstance()->start([output] { output->stop(); });
            stopSource(item);
        }
        const auto old = sources;
        { std::lock_guard lock(sourcesMutex); sources = state.sources; }
        for (const auto &item : sources) if (std::find(old.begin(), old.end(), item) == old.end()) startSource(item);
        scenes = state.scenes; selectedScene = std::clamp(state.selectedScene, 0, int(scenes.size())-1); selectedSource = state.selectedSource;
        // An undo must never leave Program holding references to deleted sources.
        if (studioMode) for (auto &layer : program.layers) if (!source(layer.source)) layer.visible = false;
        rebuild(); changed(); updateAudioRouting(); refreshGuestReturns(); restoring = false;
        undoButton->setEnabled(historyIndex > 0); redoButton->setEnabled(historyIndex+1 < history.size());
    }
    void record() {
        if (recorder.active()) { recorder.requestStop(); return; }
        if (recorder.busy()) return;
        const auto path = QFileDialog::getSaveFileName(q, "Record Program", QStandardPaths::writableLocation(QStandardPaths::MoviesLocation)+"/Desktop Studio recording.mkv", "Matroska video (*.mkv)");
        if (path.isEmpty()) return;
        recorder.start(path.endsWith(".mkv", Qt::CaseInsensitive) ? path : path+".mkv", Width, Height, canvasFps);
        recordingStatus->setText(recorder.status());
    }
    void updateModeUi() {
        audioBox->setMinimumWidth(340);
        audioBox->show();
        programPanel->setVisible(studioMode);
        takeButton->setVisible(studioMode); fadeButton->setVisible(studioMode);
        brand->setText(QString("<b style='font-size:22px'>Desktop Studio</b><br>") +
            (studioMode ? "Studio mode: prepare in Preview, then Take to output." : "Single canvas: scene changes and edits go straight to output."));
        sourceBox->setTitle(studioMode ? "Sources - checked items appear in Preview" : "Sources - checked items appear in output");
        previewView->setAccessibleName(studioMode ? "Preview canvas" : "Output canvas - edits go live");
    }
    void message(const QString &text) { status->setText(text); }

    void buildUi() {
        q->setMinimumSize(1100, 620);
        q->setStyleSheet("QPushButton { background:#192b3d; color:#eaf4ff; border:1px solid #304358; border-radius:4px; padding:8px; }"
            "QPushButton:hover { background:#253c52; } QPushButton#takeScene, QPushButton#mixerOutput { background:#00c2ff; color:#06131a; font-weight:700; }"
            "QListWidget::indicator { width:14px; height:14px; border:1px solid #647b91; border-radius:3px; }"
            "QListWidget::indicator:checked { background:#00c2ff; } QListWidget::indicator:unchecked { background:#101b27; }");
        auto *root = new QVBoxLayout(q); root->setContentsMargins(18, 14, 18, 14); root->setSpacing(12);
        auto *toolbar = new QHBoxLayout;
        brand = new QLabel(q);
        toolbar->addWidget(brand); toolbar->addStretch();
        auto *invite = button("Invite guest", "inviteGuest", toolbar, q);
        auto *wireless = button("Wireless camera", "inviteCamera", toolbar, q);
        auto *chat = button("Social chat", "socialChat", toolbar, q);
        auto *talk = button("Guest talkback", "guestTalkback", toolbar, q);
        auto *output = button("Output settings", "mixerOutput", toolbar, q);
        QObject::connect(invite, &QPushButton::clicked, q, [this] { addInvite(false); });
        QObject::connect(wireless, &QPushButton::clicked, q, [this] { addInvite(true); });
        QObject::connect(chat, &QPushButton::clicked, q, [this] { addChat(); });
        talk->setText("Private talkback"); talk->setCheckable(true);
        talk->setToolTip("While enabled, microphone inputs go only to guests and are removed from Program. Add your microphone as a source first.");
        QObject::connect(talk, &QPushButton::toggled, q, [this](bool enabled) {
            privateTalkback = enabled;
            message(enabled ? "PRIVATE TALKBACK: microphone inputs are off air and sent only to guests. Click Private talkback again to return them to Program." : "Private talkback ended. Normal microphone routing restored.");
        });
        QObject::connect(output, &QPushButton::clicked, q, [this] { emit q->outputRequested(); });
        root->addLayout(toolbar);
        auto *outputBar = new QHBoxLayout;
        startOutput = button("Start output", "studioStartOutput", outputBar, q);
        recordOutput = button("Record...", "studioRecord", outputBar, q);
        outputStatus = new QLabel("Configure a destination in Output settings, or record to a file.", q);
        outputStatus->setObjectName("studioOutputStatus"); outputStatus->setWordWrap(true);
        outputBar->addWidget(outputStatus, 1); root->addLayout(outputBar);
        QObject::connect(startOutput, &QPushButton::clicked, q, [this] { emit q->toggleOutputRequested(); });
        QObject::connect(recordOutput, &QPushButton::clicked, q, [this] { record(); });
        recordingStatus = new QLabel(q); recordingStatus->setObjectName("recordingStatus"); recordingStatus->setWordWrap(true); root->addWidget(recordingStatus);
        canvasFormat = new QComboBox(q); canvasFormat->setObjectName("canvasFormat");
        for (const auto &format : {"1280x720 / 30 fps", "1920x1080 / 30 fps", "1920x1080 / 60 fps", "1280x720 / 60 fps", "1080x1920 / 30 fps"}) canvasFormat->addItem(format);
        outputBar->addWidget(canvasFormat);
        auto *localBar = new QHBoxLayout;
        spoutOutput = new QCheckBox("Spout output", q); spoutOutput->setObjectName("spoutOutput"); localBar->addWidget(spoutOutput);
        virtualCamera = new QCheckBox("Virtual camera", q); virtualCamera->setObjectName("virtualCamera"); localBar->addWidget(virtualCamera);
        auto *removeCamera = button("Unregister camera", "unregisterCamera", localBar, q);
        guestReturnEnabled = new QCheckBox("Guest Program return", q); guestReturnEnabled->setObjectName("guestProgramReturn"); localBar->addWidget(guestReturnEnabled);
        QObject::connect(guestReturnEnabled, &QCheckBox::toggled, q, [this] { refreshGuestReturns(); });
        localBar->addStretch(); root->addLayout(localBar);
        QObject::connect(spoutOutput, &QCheckBox::toggled, q, [this] { localOutputs.configure(spoutOutput->isChecked(), virtualCamera->isChecked(), canvasFps); });
        QObject::connect(virtualCamera, &QCheckBox::toggled, q, [this](bool on) {
            if (on) { const auto error = LocalOutputs::registerCamera(true); if (!error.isEmpty()) { message(error); const QSignalBlocker block(virtualCamera); virtualCamera->setChecked(false); return; } }
            localOutputs.configure(spoutOutput->isChecked(), virtualCamera->isChecked(), canvasFps);
            message(on ? "Select Desktop Studio Virtual Camera in another app. Camera output carries video; select your microphone there separately." : "Virtual camera stopped.");
        });
        QObject::connect(removeCamera, &QPushButton::clicked, q, [this] { virtualCamera->setChecked(false); const auto error = LocalOutputs::registerCamera(false); message(error.isEmpty() ? "Virtual camera removed from this Windows account." : error); });
        renderer = new QComboBox(q); renderer->setObjectName("compositorBackend"); renderer->addItems({"GPU composition", "CPU composition"}); outputBar->addWidget(renderer);
        // The default 720p mix is cheaper on CPU while outputs require readback.
        // Keep GPU available for workloads where its scaling/compositing wins.
        renderer->setCurrentIndex(1);
        QObject::connect(renderer, &QComboBox::currentIndexChanged, q, [this] { compositions = {}; changed(); });
        QObject::connect(canvasFormat, &QComboBox::currentIndexChanged, q, [this](int index) {
            if (loading || rebuilding) return;
            if (core->isLive() || recorder.active()) { message("Stop streaming and recording before changing the canvas format."); return; }
            const std::array<QSize,5> sizes{{{1280,720},{1920,1080},{1920,1080},{1280,720},{1080,1920}}};
            Width = sizes[index].width(); Height = sizes[index].height(); canvasFps = (index == 2 || index == 3) ? 60 : 30;
            compositions = {}; nextRender = 0; videoTimer.setInterval(1000/canvasFps);
            for (const auto &item : sources) if (item->kind == "title" || item->kind == "color") { stopSource(item); startSource(item); }
            changed();
        });
        monitors = new QHBoxLayout;
        auto monitor = [&](const QString &name, Canvas *&view, QLabel *&title) {
            auto *panel = new QWidget(q);
            auto *box = new QVBoxLayout(panel); box->setContentsMargins(0, 0, 0, 0);
            title = new QLabel(name, q); title->setStyleSheet("font-weight:700; color:#9ab5e8");
            box->addWidget(title); view = new Canvas(q); view->setObjectName(name.toLower()+"Canvas");
            box->addWidget(view, 1); monitors->addWidget(panel, 1);
            return panel;
        };
        monitor("Preview", previewView, previewTitle);
        programPanel = monitor("Program", programView, programTitle);
        root->addLayout(monitors, 4);
        auto *transport = new QHBoxLayout;
        auto *mode = new QCheckBox("Studio mode", q); mode->setObjectName("studioMode");
        mode->setChecked(studioMode);
        mode->setToolTip("Stage scenes in Preview before taking them live. Turning Studio mode off takes Preview to output and enables direct editing.");
        transport->addWidget(mode);
        undoButton = button("Undo", "undoEdit", transport, q); redoButton = button("Redo", "redoEdit", transport, q);
        undoButton->setEnabled(false); redoButton->setEnabled(false);
        QObject::connect(undoButton, &QPushButton::clicked, q, [this] { undo(false); });
        QObject::connect(redoButton, &QPushButton::clicked, q, [this] { undo(true); });
        auto shortcut = [this](const QKeySequence &keys, std::function<void()> action) {
            auto *key = new QShortcut(keys, q); key->setContext(Qt::WidgetWithChildrenShortcut);
            QObject::connect(key, &QShortcut::activated, q, std::move(action));
        };
        shortcut(QKeySequence::Undo, [this] { undo(false); }); shortcut(QKeySequence::Redo, [this] { undo(true); });
        shortcut(QKeySequence("Ctrl+Shift+M"), [this] { if (auto item = source(selectedSource)) { item->muted = !item->muted; rebuildAudio(); changed(); } });
        shortcut(QKeySequence("Ctrl+Return"), [this] { if (studioMode) takeScene(false); });
        for (int i = 0; i < 9; ++i) shortcut(QKeySequence(QString("Ctrl+%1").arg(i+1)), [this, i] {
            if (i < int(scenes.size())) sceneList->setCurrentRow(i);
        });
        QObject::connect(mode, &QCheckBox::toggled, q, [this](bool enabled) {
            studioMode = enabled; draggingLayer = false;
            QSettings("VDO.Ninja", "Video Mixer").setValue("mixer/studioMode", enabled);
            changed(); updateModeUi(); rebuildSources();
            message(enabled ? "Studio mode: layout edits stay in Preview until you Take." : "Single canvas: the selected scene is on output. Edits apply immediately.");
        });
        takeButton = button("Take / Cut", "takeScene", transport, q);
        fadeButton = button("Fade (400 ms)", "fadeScene", transport, q);
        transport->addStretch();
        auto *popout = button("Program window", "programWindow", transport, q);
        auto *exportShow = button("Save show as...", "exportShow", transport, q);
        auto *importShow = button("Open show...", "importShow", transport, q);
        QObject::connect(takeButton, &QPushButton::clicked, q, [this] { takeScene(false); });
        QObject::connect(fadeButton, &QPushButton::clicked, q, [this] { takeScene(true); });
        QObject::connect(popout, &QPushButton::clicked, q, [this] {
            if (!outputView) {
                outputView = new Canvas;
                outputView->setWindowTitle("Desktop Studio - Program");
                outputView->setAttribute(Qt::WA_DeleteOnClose);
                outputView->setAttribute(Qt::WA_QuitOnClose, false);
                outputView->resize(1280, 720);
                auto *full = new QShortcut(QKeySequence(Qt::Key_F11), outputView);
                QObject::connect(full, &QShortcut::activated, outputView, [this] {
                    if (outputView->isFullScreen()) outputView->showNormal(); else outputView->showFullScreen();
                });
            }
            outputView->show(); outputView->raise();
        });
        QObject::connect(exportShow, &QPushButton::clicked, q, [this] {
            const auto path = QFileDialog::getSaveFileName(q, "Save show (includes source URLs and invite links)", {}, "Mixer show (*.mixer.json)");
            if (!path.isEmpty() && save(path)) message("Show saved. Keep this file private if its sources include access links.");
        });
        QObject::connect(importShow, &QPushButton::clicked, q, [this] {
            if (core->isLive() || recorder.busy() || virtualCamera->isChecked() || spoutOutput->isChecked()) { message("Stop streaming, recording and local outputs before opening another show."); return; }
            const auto path = QFileDialog::getOpenFileName(q, "Open show", {}, "Mixer show (*.mixer.json)");
            if (!path.isEmpty() && load(path)) {
                selectedScene = 0; selectedSource.clear(); program = scenes[0]; fading = false;
                history.clear(); historyIndex = 0; rebuild(); updateAudioRouting(); changed();
            }
        });
        root->addLayout(transport);

        panels = new QSplitter(Qt::Horizontal, q);
        auto *sceneBox = new QGroupBox("Scenes", panels); auto *sceneLayout = new QVBoxLayout(sceneBox);
        sceneList = new QListWidget(sceneBox); sceneList->setObjectName("sceneList"); sceneLayout->addWidget(sceneList);
        auto *sceneButtons = new QHBoxLayout;
        auto *newScene = button("+", "addScene", sceneButtons, q);
        auto *duplicate = button("Duplicate", "duplicateScene", sceneButtons, q);
        auto *rename = button("Rename", "renameScene", sceneButtons, q);
        auto *removeScene = button("-", "removeScene", sceneButtons, q); sceneLayout->addLayout(sceneButtons);
        auto *background = new QPushButton("Background color", q); sceneLayout->addWidget(background);
        QObject::connect(sceneList, &QListWidget::itemSelectionChanged, q, [this] {
            const int row = sceneList->selectedItems().isEmpty() ? -1 : sceneList->row(sceneList->selectedItems().front());
            if (rebuilding || row < 0 || row >= static_cast<int>(scenes.size())) return;
            selectedScene = row; rebuildSources(); syncInspector(); changed();
        });
        QObject::connect(newScene, &QPushButton::clicked, q, [this] {
            if (scenes.size() >= 64) { message("A show supports up to 64 scenes."); return; }
            bool ok; const auto name = QInputDialog::getText(q, "New scene", "Scene name", QLineEdit::Normal, "Scene " + QString::number(scenes.size()+1), &ok);
            if (!ok || name.trimmed().isEmpty()) return;
            Scene scene; scene.name = name.trimmed(); scenes.push_back(scene); selectedScene = static_cast<int>(scenes.size())-1; rebuild(); changed();
        });
        QObject::connect(duplicate, &QPushButton::clicked, q, [this] {
            if (scenes.size() >= 64) { message("A show supports up to 64 scenes."); return; }
            Scene scene = preview(); scene.id = newId(); scene.name += " copy"; scenes.push_back(scene);
            selectedScene = static_cast<int>(scenes.size())-1; rebuild(); changed();
        });
        QObject::connect(rename, &QPushButton::clicked, q, [this] {
            bool ok; const auto name = QInputDialog::getText(q, "Rename scene", "Scene name", QLineEdit::Normal, preview().name, &ok);
            if (ok && !name.trimmed().isEmpty()) { preview().name = name.trimmed(); rebuild(); changed(); }
        });
        QObject::connect(removeScene, &QPushButton::clicked, q, [this] {
            if (scenes.size() == 1) { message("Keep at least one scene."); return; }
            scenes.erase(scenes.begin()+selectedScene); selectedScene = 0; rebuild(); changed();
        });
        QObject::connect(background, &QPushButton::clicked, q, [this] {
            const QColor color = QColorDialog::getColor(preview().background, q);
            if (color.isValid()) { preview().background = color; changed(); }
        });

        sourceBox = new QGroupBox("Sources", panels);
        auto *sourceLayout = new QVBoxLayout(sourceBox);
        sourceList = new QListWidget(sourceBox); sourceList->setObjectName("mixerSources"); sourceLayout->addWidget(sourceList);
        auto *sourceButtons = new QHBoxLayout;
        auto *add = button("+ Add source", "addSource", sourceButtons, q);
        auto *remove = button("Remove", "removeSource", sourceButtons, q);
        auto *interact = button("Open / Invite", "interactSource", sourceButtons, q);
        sourceLayout->addLayout(sourceButtons);
        auto *editButtons = new QHBoxLayout;
        auto *edit = button("Edit source", "editSource", editButtons, q);
        auto *reconnect = button("Reconnect", "reconnectSource", editButtons, q);
        cameraSettingsButton = button("Camera settings...", "cameraSettings", editButtons, q);
        cameraSettingsButton->setEnabled(false);
        auto *admit = button("Admit / Backstage", "admitGuest", sourceButtons, q);
        QObject::connect(admit, &QPushButton::clicked, q, [this] {
            const auto item = source(selectedSource); if (!item || (item->kind != "guest" && item->kind != "wireless")) { message("Select a guest or wireless camera."); return; }
            item->admitted = !item->admitted;
            if (studioMode || !item->admitted) {
                for (auto *scene : {&program, &previous}) for (auto &layer : scene->layers) if (layer.source == item->id) layer.visible = false;
            }
            if (item->admitted) {
                auto *layer = selectedLayer(); if (layer) layer->visible = true; else preview().layers.push_back({item->id});
            }
            updateAudioRouting(); rebuildSources(); changed();
            message(item->admitted ? (studioMode ? "Guest admitted to Preview. Take when ready." : "Guest is on air.") : "Guest is backstage. Picture and audio are off air; Program return continues.");
        });
        auto *playback = button("Playback", "mediaPlayback", editButtons, q);
        QObject::connect(playback, &QPushButton::clicked, q, [this] {
            auto item = source(selectedSource); if (!item || !item->media) { message("Select a video or audio file source."); return; }
            QDialog dialog(q); dialog.setWindowTitle("Media playback"); auto *layout = new QVBoxLayout(&dialog);
            auto *pause = new QCheckBox("Paused", &dialog); pause->setObjectName("mediaPaused"); pause->setChecked(item->mediaPaused || item->media->ended()); layout->addWidget(pause);
            auto *loop = new QCheckBox("Loop", &dialog); loop->setObjectName("mediaLoop"); loop->setChecked(item->mediaLoop); layout->addWidget(loop);
            auto *restart = new QPushButton("Restart from beginning", &dialog); restart->setObjectName("mediaRestart"); layout->addWidget(restart);
            QObject::connect(pause, &QCheckBox::toggled, &dialog, [this, item](bool value) { item->mediaPaused = value; if (item->media) item->media->setPaused(value); changed(); });
            QObject::connect(loop, &QCheckBox::toggled, &dialog, [this, item](bool value) { item->mediaLoop = value; if (item->media) item->media->setLoop(value); changed(); });
            QObject::connect(restart, &QPushButton::clicked, &dialog, [item, pause] { pause->setChecked(false); if (item->media) item->media->restart(); });
            QDialogButtonBox close(QDialogButtonBox::Close); layout->addWidget(&close); QObject::connect(&close, &QDialogButtonBox::rejected, &dialog, &QDialog::reject); dialog.exec();
        });
        QObject::connect(cameraSettingsButton, &QPushButton::clicked, q, [this] { openCameraSettings(); });
        sourceLayout->addLayout(editButtons);
        sourceHealth = new QLabel("Select a source to inspect its status.", q);
        sourceHealth->setObjectName("sourceHealth"); sourceHealth->setWordWrap(true); sourceLayout->addWidget(sourceHealth);
        QObject::connect(edit, &QPushButton::clicked, q, [this] { addSourceDialog(true); });
        QObject::connect(reconnect, &QPushButton::clicked, q, [this] {
            if (auto item = source(selectedSource)) {
                if (item->kind == "window") addSourceDialog(true, true);
                else { stopSource(item); startSource(item); message("Reconnecting " + item->name); }
            }
        });
        QObject::connect(add, &QPushButton::clicked, q, [this] { addSourceDialog(); });
        QObject::connect(remove, &QPushButton::clicked, q, [this] { removeSource(); });
        QObject::connect(interact, &QPushButton::clicked, q, [this] {
            auto item = source(selectedSource); if (!item) return;
            if (!item->invite.isEmpty()) showInvite(item);
            else if (item->kind == "chat" && QUrlQuery(QUrl(item->locator)).hasQueryItem("session")) {
                QUrl dock("https://socialstream.ninja/dock.html");
                QUrlQuery query;
                const QUrlQuery overlayQuery(QUrl(item->locator));
                for (const auto &key : {"session", "password", "server", "localserver"})
                    if (overlayQuery.hasQueryItem(key)) query.addQueryItem(key, overlayQuery.queryItemValue(key, QUrl::FullyDecoded));
                dock.setQuery(query);
                delete chatDock.data();
                chatDock = new BrowserSource("chat-dock", dock, false);
                chatDock->setWindowTitle("Desktop Studio - Social Stream chat dock");
                chatDock->interact();
            }
            else if (item->browser) item->browser->interact();
            else message(item->error.isEmpty() ? item->name : item->error);
        });
        QObject::connect(sourceList, &QListWidget::itemSelectionChanged, q, [this] {
            if (rebuilding) return;
            auto *item = sourceList->selectedItems().isEmpty() ? nullptr : sourceList->selectedItems().front();
            selectedSource = item ? item->data(Qt::UserRole).toString() : QString(); syncInspector();
            if (auto selected = source(selectedSource); selected && !selected->error.isEmpty()) message(selected->error);
        });
        QObject::connect(sourceList, &QListWidget::itemChanged, q, [this](QListWidgetItem *item) {
            if (rebuilding) return;
            const auto id = item->data(Qt::UserRole).toString();
            auto &layers = preview().layers;
            auto found = std::find_if(layers.begin(), layers.end(), [&](const auto &l) { return l.source == id; });
            if (found != layers.end()) found->visible = item->checkState() == Qt::Checked;
            else if (item->checkState() == Qt::Checked) layers.push_back({id});
            syncInspector(); changed();
        });
        auto *layouts = new QHBoxLayout;
        for (const auto &name : {"Full", "Side by side", "Picture in picture"}) {
            auto *preset = button(name, QString("layout-")+name, layouts, q);
            QObject::connect(preset, &QPushButton::clicked, q, [this, name] { applyLayout(name); });
        }
        sourceLayout->addLayout(layouts);
        auto *geometry = new QHBoxLayout;
        auto makeCoordinate = [&](const QString &label, QDoubleSpinBox *&spin) {
            geometry->addWidget(new QLabel(label, q)); spin = new QDoubleSpinBox(q);
            spin->setObjectName("layer"+label); spin->setRange(label == "X" || label == "Y" ? 0 : 1, 100);
            spin->setSuffix("%"); spin->setDecimals(1); geometry->addWidget(spin);
            QObject::connect(spin, &QDoubleSpinBox::valueChanged, q, [this] {
                if (rebuilding) return;
                if (auto *layer = selectedLayer(); layer && !layer->locked) {
                    layer->rect = QRectF(x->value()/100, y->value()/100, w->value()/100, h->value()/100); changed();
                }
            });
        };
        makeCoordinate("X", x); makeCoordinate("Y", y); makeCoordinate("W", w); makeCoordinate("H", h);
        sourceLayout->addLayout(geometry);
        auto *transform = new QHBoxLayout;
        layerLock = new QCheckBox("Lock", q); layerLock->setObjectName("layerLock"); transform->addWidget(layerLock);
        snap = new QCheckBox("Snap", q); snap->setObjectName("snapLayers"); snap->setChecked(true); transform->addWidget(snap);
        fitMode = new QComboBox(q); fitMode->setObjectName("layerFit");
        fitMode->addItem("Fit", "fit"); fitMode->addItem("Fill", "fill"); fitMode->addItem("Stretch", "stretch"); transform->addWidget(fitMode);
        sourceLayout->addLayout(transform);
        QObject::connect(layerLock, &QCheckBox::toggled, q, [this](bool locked) { if (!rebuilding) if (auto *layer = selectedLayer()) { layer->locked = locked; syncInspector(); changed(); } });
        QObject::connect(fitMode, &QComboBox::currentIndexChanged, q, [this] { if (!rebuilding) if (auto *layer = selectedLayer(); layer && !layer->locked) { layer->fit = fitMode->currentData().toString(); changed(); } });
        auto *cropRow = new QHBoxLayout;
        int edge = 0;
        for (const auto &label : {"Left", "Top", "Right", "Bottom"}) {
            auto *spin = new QDoubleSpinBox(q); spin->setObjectName(QString("crop")+label); spin->setRange(0, 95); spin->setSuffix("%"); spin->setDecimals(1);
            cropControls[edge++] = spin; cropRow->addWidget(new QLabel(label, q)); cropRow->addWidget(spin);
            QObject::connect(spin, &QDoubleSpinBox::valueChanged, q, [this] {
                if (rebuilding) return;
                if (auto *layer = selectedLayer(); layer && !layer->locked) {
                    const double left = cropControls[0]->value()/100, top = cropControls[1]->value()/100;
                    layer->crop = {left, top, std::max(.01, 1-left-cropControls[2]->value()/100), std::max(.01, 1-top-cropControls[3]->value()/100)};
                    syncInspector(); changed();
                }
            });
        }
        sourceLayout->addLayout(cropRow);
        auto *layerButtons = new QHBoxLayout;
        auto *front = button("Bring forward", "layerForward", layerButtons, q);
        auto *back = button("Send back", "layerBack", layerButtons, q);
        layerButtons->addWidget(new QLabel("Opacity", q));
        opacity = new QSlider(Qt::Horizontal, q); opacity->setRange(0, 100); opacity->setValue(100); layerButtons->addWidget(opacity);
        sourceLayout->addLayout(layerButtons);
        QObject::connect(front, &QPushButton::clicked, q, [this] { moveLayer(1); });
        QObject::connect(back, &QPushButton::clicked, q, [this] { moveLayer(-1); });
        QObject::connect(opacity, &QSlider::valueChanged, q, [this](int value) {
            if (!rebuilding) if (auto *layer = selectedLayer(); layer && !layer->locked) { layer->opacity = value/100.0; changed(); }
        });
        previewView->pressed = [this](QPointF point) {
            draggingLayer = false; resizeCorner = -1;
            if (auto *layer = selectedLayer(); layer && layer->visible && !layer->locked) {
                const auto bounds = previewView->viewport(); const auto rect = layer->rect;
                const std::array<QPointF,4> corners{rect.topLeft(), rect.topRight(), rect.bottomLeft(), rect.bottomRight()};
                for (int i = 0; i < 4; ++i) if (std::abs(corners[i].x()-point.x())*bounds.width() <= 9 && std::abs(corners[i].y()-point.y())*bounds.height() <= 9) resizeCorner = i;
                if (resizeCorner >= 0) { draggingLayer = true; dragRect = rect; dragStart = point; return; }
            }
            for (auto it = preview().layers.rbegin(); it != preview().layers.rend(); ++it) {
                const auto item = source(it->source);
                if (!it->locked && it->visible && it->opacity > 0 && item && item->hasVideo() && it->rect.contains(point)) {
                    selectedSource = it->source; dragRect = it->rect; dragStart = point; draggingLayer = true;
                    rebuildSources(); syncInspector(); break;
                }
            }
        };
        previewView->dragged = [this](QPointF point) {
            if (!draggingLayer) return;
            if (auto *layer = selectedLayer(); layer && !layer->locked) {
                const auto delta = point-dragStart;
                auto snapCoordinate = [this](double value, bool horizontal) {
                    if (!snap->isChecked() || QApplication::keyboardModifiers().testFlag(Qt::AltModifier)) return value;
                    std::vector<double> guides{0, .5, 1};
                    for (const auto &other : preview().layers) if (other.source != selectedSource && other.visible) {
                        guides.push_back(horizontal ? other.rect.left() : other.rect.top());
                        guides.push_back(horizontal ? other.rect.right() : other.rect.bottom());
                    }
                    for (double guide : guides) if (std::abs(value-guide) < .008) return guide;
                    return value;
                };
                if (resizeCorner >= 0) {
                    QRectF rect = dragRect;
                    if (resizeCorner == 0 || resizeCorner == 2) rect.setLeft(std::clamp(snapCoordinate(point.x(), true), 0.0, rect.right()-.01));
                    else rect.setRight(std::clamp(snapCoordinate(point.x(), true), rect.left()+.01, 1.0));
                    if (resizeCorner < 2) rect.setTop(std::clamp(snapCoordinate(point.y(), false), 0.0, rect.bottom()-.01));
                    else rect.setBottom(std::clamp(snapCoordinate(point.y(), false), rect.top()+.01, 1.0));
                    layer->rect = rect;
                } else {
                    double left = snapCoordinate(dragRect.x()+delta.x(), true), top = snapCoordinate(dragRect.y()+delta.y(), false);
                    const double right = snapCoordinate(left+dragRect.width(), true), bottom = snapCoordinate(top+dragRect.height(), false);
                    layer->rect.moveTopLeft({std::clamp(right-dragRect.width(), 0.0, std::max(0.0, 1-dragRect.width())),
                        std::clamp(bottom-dragRect.height(), 0.0, std::max(0.0, 1-dragRect.height()))});
                }
                syncInspector(); changed();
            }
        };
        previewView->released = [this] { draggingLayer = false; remember(); };

        audioBox = new QGroupBox("Audio mixer", panels); auto *audioLayout = new QVBoxLayout(audioBox);
        auto *scroll = new QScrollArea(audioBox); scroll->setObjectName("audioTracksScroll"); scroll->setWidgetResizable(true);
        scroll->setStyleSheet("QScrollArea { border:1px solid #50667b; background:#101b27; }");
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        scroll->setMinimumHeight(170);
        auto *audioWidget = new QWidget(scroll); audioWidget->setObjectName("studioAudioTracks");
        audioWidget->setStyleSheet("QWidget#studioAudioTracks { background:#080f17; }");
        audioRows = new QVBoxLayout(audioWidget); scroll->setWidget(audioWidget);
        audioLayout->addWidget(scroll);
        auto *master = new QHBoxLayout; master->addWidget(new QLabel("Master", q));
        auto *masterSlider = new QSlider(Qt::Horizontal, q); masterSlider->setRange(0, 150); masterSlider->setValue(100);
        masterSlider->setObjectName("masterGain"); master->addWidget(masterSlider);
        audioLayout->addLayout(master);
        masterMeter = new AudioLevelMeter(audioBox); masterMeter->setObjectName("masterAudioMeter");
        masterMeter->setAccessibleName("Master audio level (dBFS)"); audioLayout->addWidget(masterMeter);
        QObject::connect(masterSlider, &QSlider::valueChanged, q, [this](int value) { masterGain = value/100.0f; changed(); });
        monitorEnable = new QCheckBox("Headphone monitor", q); monitorEnable->setObjectName("monitorEnable");
        monitorDevice = new QComboBox(q); monitorDevice->setObjectName("monitorDevice");
        for (const auto &[id, name] : AudioMonitor::devices()) monitorDevice->addItem(name, id);
        audioLayout->addWidget(monitorEnable); audioLayout->addWidget(monitorDevice);
        auto *monitorVolume = new QSlider(Qt::Horizontal, q); monitorVolume->setRange(0, 100); monitorVolume->setValue(50);
        monitorVolume->setAccessibleName("Headphone volume"); monitorVolume->setObjectName("monitorVolume"); audioLayout->addWidget(monitorVolume);
        QObject::connect(monitorEnable, &QCheckBox::toggled, q, [this](bool value) { monitoring = value; });
        QObject::connect(monitorDevice, &QComboBox::currentIndexChanged, q, [this] { monitorDeviceId = monitorDevice->currentData().toInt(); });
        QObject::connect(monitorVolume, &QSlider::valueChanged, q, [this](int value) { monitorGain = value/100.0f; });
        auto *monitorHelp = new QLabel("Scene follows Program. Listen also hears sources off air. Use headphones for monitoring and guest talkback. Mics start with Listen off.", q);
        monitorHelp->setWordWrap(true); audioLayout->addWidget(monitorHelp);
        // Each tool panel has its own bounded scroll surface. The canvas stays
        // in view on laptop displays while inspectors can scroll independently.
        for (auto *group : {sceneBox, sourceBox, audioBox}) {
            auto *scroll = new QScrollArea(q); scroll->setObjectName(group == sceneBox ? "scenesPanelScroll" : group == sourceBox ? "sourcesPanelScroll" : "audioPanelScroll");
            scroll->setWidgetResizable(true); scroll->setMinimumSize(180, 210);
            scroll->setStyleSheet("QScrollArea { border:1px solid #50667b; background:#101b27; }");
            group->setObjectName(group == sceneBox ? "studioScenesPanel" : group == sourceBox ? "studioSourcesPanel" : "studioAudioPanel");
            group->setStyleSheet(QString("QGroupBox#%1 { background:#0b141e; border:1px solid #50667b; border-radius:4px; margin-top:8px; } QGroupBox#%1::title { subcontrol-origin:margin; left:8px; padding:0 3px; }").arg(group->objectName()));
            scroll->setWidget(group); panels->addWidget(scroll);
        }
        panels->setSizes({220, 680, 380}); root->addWidget(panels, 4);
        status = new QLabel("Add cameras, windows, audio, or browser sources to get started.", q);
        status->setObjectName("mixerStatus"); status->setWordWrap(true); status->setTextInteractionFlags(Qt::TextSelectableByMouse);
        root->addWidget(status);
    }

    void rebuildSources() {
        rebuilding = true; sourceList->clear();
        for (const auto &item : sources) {
            auto *row = new QListWidgetItem(item->name + "  [" + item->kind + "]", sourceList);
            row->setData(Qt::UserRole, item->id); row->setFlags(row->flags() | Qt::ItemIsUserCheckable);
            const auto found = std::find_if(preview().layers.begin(), preview().layers.end(), [&](const auto &l) { return l.source == item->id && l.visible; });
            row->setCheckState(found == preview().layers.end() ? Qt::Unchecked : Qt::Checked);
            row->setToolTip(item->error.isEmpty() ? (studioMode ? "Toggle to include in Preview. Take to update Program." : "Toggle to show or hide this source in output immediately.") : item->error);
            if (item->id == selectedSource) sourceList->setCurrentItem(row);
        }
        rebuilding = false;
    }
    void rebuild() {
        rebuilding = true; sceneList->clear();
        for (const auto &scene : scenes) sceneList->addItem(scene.name);
        sceneList->setCurrentRow(selectedScene); rebuilding = false;
        rebuildSources(); rebuildAudio(); syncInspector();
    }
    void syncInspector() {
        rebuilding = true; const auto *layer = selectedLayer();
        const auto selected = source(selectedSource);
        cameraSettingsButton->setEnabled(selected && selected->kind == "camera");
        for (auto *spin : {x, y, w, h}) spin->setEnabled(layer && !layer->locked);
        layerLock->setEnabled(layer); layerLock->setChecked(layer && layer->locked); fitMode->setEnabled(layer && !layer->locked);
        for (auto *spin : cropControls) spin->setEnabled(layer && !layer->locked);
        if (layer) {
            fitMode->setCurrentIndex(std::max(0, fitMode->findData(layer->fit)));
            cropControls[0]->setValue(layer->crop.left()*100); cropControls[1]->setValue(layer->crop.top()*100);
            cropControls[2]->setValue((1-layer->crop.right())*100); cropControls[3]->setValue((1-layer->crop.bottom())*100);
        }
        opacity->setEnabled(layer && !layer->locked);
        if (layer) {
            x->setValue(layer->rect.x()*100); y->setValue(layer->rect.y()*100);
            w->setValue(layer->rect.width()*100); h->setValue(layer->rect.height()*100); opacity->setValue(qRound(layer->opacity*100));
        }
        rebuilding = false;
    }
    void rebuildAudio() {
        meters.clear();
        while (auto *item = audioRows->takeAt(0)) { delete item->widget(); delete item; }
        for (const auto &item : sources) {
            if (!item->hasAudio()) continue;
            auto *row = new QWidget(q); row->setObjectName("audioTrack_"+item->id);
            auto *layout = new QVBoxLayout(row); layout->setContentsMargins(8, 8, 8, 10); layout->setSpacing(6);
            auto *name = new QLabel(item->name, row); name->setWordWrap(true); layout->addWidget(name);
            auto *meter = new AudioLevelMeter(row); meter->setObjectName("audioMeter_"+item->id);
            meter->setAccessibleName(item->name+" audio level (dBFS)"); layout->addWidget(meter);
            meters.emplace_back(item, meter);
            auto *controls = new QHBoxLayout;
            auto *gain = new QSlider(Qt::Horizontal, row); gain->setRange(0, 200); gain->setValue(qRound(item->gain*100));
            gain->setAccessibleName(item->name+" volume"); controls->addWidget(gain);
            auto *mute = new QCheckBox("Mute", row); mute->setChecked(item->muted); controls->addWidget(mute);
            auto *follow = new QCheckBox("Scene", row); follow->setChecked(item->followsScene); controls->addWidget(follow);
            auto *listen = new QCheckBox("Listen", row); listen->setChecked(item->monitored); controls->addWidget(listen);
            layout->addLayout(controls);
            auto *effects = new QHBoxLayout;
            auto *noise = new QCheckBox("Noise suppression", row); noise->setChecked(item->noiseSuppression); effects->addWidget(noise);
            auto *gate = new QCheckBox("Gate", row); gate->setChecked(item->noiseGate); effects->addWidget(gate);
            auto *compress = new QCheckBox("Compress", row); compress->setChecked(item->compression); effects->addWidget(compress);
            noise->setToolTip("RNNoise speech noise suppression. Leave off for music and game audio.");
            gate->setToolTip("Soft noise gate at -45 dBFS."); compress->setToolTip("4:1 compression above -18 dBFS, without automatic makeup gain.");
            QObject::connect(noise, &QCheckBox::toggled, q, [this, item](bool value) { item->noiseSuppression = value; changed(); });
            QObject::connect(gate, &QCheckBox::toggled, q, [this, item](bool value) { item->noiseGate = value; changed(); });
            QObject::connect(compress, &QCheckBox::toggled, q, [this, item](bool value) { item->compression = value; changed(); });
            layout->addLayout(effects); audioRows->addWidget(row);
            QObject::connect(gain, &QSlider::valueChanged, q, [this, item](int value) { item->gain = value/100.0f; changed(); });
            QObject::connect(mute, &QCheckBox::toggled, q, [this, item](bool value) { item->muted = value; changed(); });
            QObject::connect(follow, &QCheckBox::toggled, q, [this, item](bool value) { item->followsScene = value; changed(); });
            QObject::connect(listen, &QCheckBox::toggled, q, [this, item](bool value) { item->monitored = value; changed(); });
        }
        audioRows->addStretch();
    }

    void applyCameraControl(const std::shared_ptr<Source> &item, const QString &id, int value, bool automatic,
                            std::function<void(const video::CameraControlResult&)> done = {}, bool persist = true) {
        if (!item->camera) { if (done) done({false, {}, "Camera is not running."}); return; }
        const auto generation = item->cameraGeneration;
        auto future = std::make_shared<std::future<video::CameraControlResult>>(item->camera->setControl(id.toStdString(), value, automatic));
        auto *poll = new QTimer(q);
        std::weak_ptr<Source> weak = item;
        QObject::connect(poll, &QTimer::timeout, q, [this, weak, generation, id, future, poll, done, persist] {
            if (future->wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
            poll->stop(); poll->deleteLater();
            auto result = future->get();
            const auto item = weak.lock();
            if (!item || item->cameraGeneration != generation) result = {false, {}, "Camera changed before the setting completed."};
            if (result.success && persist) {
                item->cameraControls[id] = QJsonObject{{"value", result.control.value}, {"auto", result.control.automatic}};
                changed();
            } else if (!result.success && item) message(item->name+": "+QString::fromStdString(result.error));
            if (done) done(result);
        });
        poll->start(25);
    }

    void openCameraSettings() {
        const auto item = source(selectedSource);
        if (!item || item->kind != "camera") return;
        if (!item->camera || !item->camera->isCapturing()) {
            message(item->name+": Camera is not running. Use Reconnect or select another device in Edit source."); return;
        }
        QDialog dialog(q); dialog.setWindowTitle("Camera settings - "+item->name); dialog.resize(1100, 670);
        QPointer<QDialog> guard(&dialog);
        auto *root = new QVBoxLayout(&dialog);
        auto *modeRow = new QHBoxLayout;
        modeRow->addWidget(new QLabel("Capture mode", &dialog));
        auto *mode = new QComboBox(&dialog); mode->setObjectName("cameraMode"); modeRow->addWidget(mode, 1);
        auto *applyMode = button("Apply capture mode", "cameraApplyMode", modeRow, &dialog);
        root->addLayout(modeRow);
        auto *active = new QLabel(&dialog); active->setObjectName("cameraActiveMode"); root->addWidget(active);
        auto *body = new QHBoxLayout;
        auto *left = new QVBoxLayout;
        auto *picture = new QLabel(&dialog); picture->setObjectName("cameraPreview"); picture->setFixedSize(400, 225);
        picture->setAlignment(Qt::AlignCenter); picture->setStyleSheet("background:#080d12"); left->addWidget(picture);
        auto *help = new QLabel("Settings change the physical camera immediately, including when it is on Program.\n\n"
            "Capture mode restarts this camera briefly. Program uses the canvas format selected in Studio.\n\n"
            "Only controls supported by this camera are shown. Turn Auto off for manual adjustments. "
            "Exposure can affect frame rate in low light.\n\n"
            "Changes are saved with this source and reapplied on reconnect. Other apps may share these camera settings.", &dialog);
        help->setWordWrap(true); help->setMaximumWidth(400); left->addWidget(help); left->addStretch(); body->addLayout(left);
        auto *scroll = new QScrollArea(&dialog); scroll->setObjectName("cameraControlsScroll"); scroll->setWidgetResizable(true); body->addWidget(scroll, 1); root->addLayout(body, 1);
        scroll->setStyleSheet("QScrollArea { border:1px solid #50667b; background:#101b27; }");
        auto *feedback = new QLabel("Adjust a setting to see the result in the live preview.", &dialog);
        feedback->setObjectName("cameraSettingsStatus"); feedback->setWordWrap(true); root->addWidget(feedback);
        QDialogButtonBox buttons(QDialogButtonBox::Close); root->addWidget(&buttons);
        QObject::connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        std::vector<video::CameraFormat> formats;
        auto formatText = [](const video::CameraFormat &format) {
            return QString("%1x%2 @ %3 fps").arg(format.width).arg(format.height).arg(format.fps, 0, 'g', 5);
        };
        auto refreshModes = [&] {
            if (!item->camera) return;
            const auto settings = item->camera->settings(); formats = settings.formats;
            mode->clear(); int selected = 0;
            for (size_t i = 0; i < formats.size(); ++i) {
                mode->addItem(formatText(formats[i]));
                if (formats[i].width == settings.activeFormat.width && formats[i].height == settings.activeFormat.height &&
                    std::abs(formats[i].fps-settings.activeFormat.fps) < .001) selected = static_cast<int>(i);
            }
            mode->setCurrentIndex(selected);
            active->setText("Active camera: "+formatText(settings.activeFormat));
        };
        auto rebuildControls = [&] {
            if (!item->camera) return;
            auto *panel = new QWidget;
            auto *rows = new QVBoxLayout(panel);
            const auto settings = item->camera->settings();
            for (const auto &control : settings.controls) {
                const auto id = QString::fromStdString(control.id);
                auto *label = new QLabel(QString::fromStdString(control.name), panel); rows->addWidget(label);
                auto *row = new QHBoxLayout;
                auto *slider = new QSlider(Qt::Horizontal, panel); slider->setObjectName("cameraSlider_"+id);
                const int steps = static_cast<int>(std::min<int64_t>((static_cast<int64_t>(control.maximum)-control.minimum)/control.step, INT_MAX));
                slider->setRange(0, steps); slider->setValue((control.value-control.minimum)/control.step);
                slider->setTracking(false); row->addWidget(slider, 1);
                auto *number = new QSpinBox(panel); number->setObjectName("cameraValue_"+id);
                number->setRange(control.minimum, control.maximum); number->setSingleStep(control.step);
                number->setValue(control.value); number->setKeyboardTracking(false); number->setMinimumWidth(78); row->addWidget(number);
                auto *automatic = new QCheckBox("Auto", panel); automatic->setObjectName("cameraAuto_"+id);
                automatic->setChecked(control.automatic); automatic->setEnabled(control.supportsAuto && control.supportsManual); row->addWidget(automatic);
                auto *reset = button("Reset", "cameraReset_"+id, row, panel);
                reset->setToolTip("Use the driver's default value, with automatic mode when supported.");
                const auto range = QString("Range %1 to %2; step %3. Values are camera driver units.").arg(control.minimum).arg(control.maximum).arg(control.step);
                number->setToolTip(range); slider->setToolTip(range); label->setToolTip(range);
                if (id == "exposure") label->setToolTip("Exposure uses powers of two seconds: -7 is 1/128 second. Longer exposure can reduce frame rate.");
                number->setEnabled(control.supportsManual && !control.automatic); slider->setEnabled(number->isEnabled());
                rows->addLayout(row);
                auto submit = [&, control, id, panel, slider, number, automatic, guard](int value, bool autoMode) {
                    // Automatic values can fall between the driver's manual steps.
                    if (!autoMode) {
                        const auto offset = static_cast<int64_t>(std::clamp(value, control.minimum, control.maximum))-control.minimum;
                        value = static_cast<int>(control.minimum+(offset/control.step)*control.step);
                    }
                    panel->setEnabled(false); applyMode->setEnabled(false);
                    feedback->setText("Applying "+QString::fromStdString(control.name)+"...");
                    applyCameraControl(item, id, value, autoMode, [&, control, panel, panelGuard = QPointer<QWidget>(panel), slider, number, automatic, guard](const auto &result) {
                        if (!guard || !panelGuard) return;
                        panel->setEnabled(true); applyMode->setEnabled(true);
                        const auto current = result.control.id.empty() ? control : result.control;
                        const QSignalBlocker blockNumber(number), blockSlider(slider), blockAuto(automatic);
                        number->setValue(current.value); slider->setValue((current.value-current.minimum)/current.step);
                        automatic->setChecked(current.automatic);
                        automatic->setEnabled(current.supportsAuto && current.supportsManual);
                        number->setEnabled(current.supportsManual && !current.automatic); slider->setEnabled(number->isEnabled());
                        feedback->setText(result.success ? QString("%1: %2%3").arg(QString::fromStdString(current.name)).arg(current.value).arg(current.automatic ? " (Auto)" : "")
                            : QString::fromStdString(result.error));
                    });
                };
                QObject::connect(slider, &QSlider::valueChanged, &dialog, [submit, control](int index) { submit(control.minimum+index*control.step, false); });
                QObject::connect(number, &QSpinBox::valueChanged, &dialog, [submit](int value) { submit(value, false); });
                QObject::connect(automatic, &QCheckBox::toggled, &dialog, [submit, number](bool on) { submit(number->value(), on); });
                QObject::connect(reset, &QPushButton::clicked, &dialog, [submit, control] { submit(control.defaultValue, control.supportsAuto); });
            }
            if (settings.controls.empty()) rows->addWidget(new QLabel("This camera's driver exposes no adjustable image controls.", panel));
            rows->addStretch(); scroll->setWidget(panel);
        };
        bool wasStarting = false;
        QObject::connect(applyMode, &QPushButton::clicked, &dialog, [&] {
            const auto index = mode->currentIndex(); if (index < 0 || static_cast<size_t>(index) >= formats.size()) return;
            const auto requested = formats[index];
            item->cameraWidth = requested.width; item->cameraHeight = requested.height; item->cameraFps = requested.fps;
            stopSource(item); startSource(item);
            wasStarting = true; feedback->setText("Applying capture mode..."); applyMode->setEnabled(false); changed();
        });
        QTimer previewTimer;
        QObject::connect(&previewTimer, &QTimer::timeout, &dialog, [&] {
            QImage image; { std::lock_guard lock(item->imageMutex); image = item->image; }
            if (!image.isNull()) picture->setPixmap(QPixmap::fromImage(image).scaled(picture->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
            if (item->starting) { wasStarting = true; applyMode->setEnabled(false); }
            else if (wasStarting) { wasStarting = false; applyMode->setEnabled(true); refreshModes(); rebuildControls(); feedback->setText(item->error.isEmpty() ? "Capture mode applied." : item->error); }
            if (item->camera && !item->starting && !item->camera->isCapturing()) feedback->setText(QString::fromStdString(item->camera->lastError()));
        });
        refreshModes(); rebuildControls(); previewTimer.start(100); dialog.exec();
    }

    void addSourceDialog(bool editing = false, bool reconnect = false) {
        const auto existing = editing ? source(selectedSource) : std::shared_ptr<Source>{};
        if (editing && !existing) { message("Select a source to edit."); return; }
        QDialog dialog(q); dialog.setWindowTitle(editing ? "Edit source" : "Add source"); auto *form = new QFormLayout(&dialog);
        QComboBox type; type.setObjectName("sourceKind");
        const std::vector<std::pair<QString, QString>> kinds = {{"Game / window", "window"}, {"Camera / webcam", "camera"},
            {"Microphone / audio input", "microphone"}, {"Desktop audio", "desktop"}, {"Spout2 feed", "spout"},
            {"Browser / remote VDO.Ninja feed", "browser"}, {"Video / audio file", "media"}, {"Image", "image"}, {"Title / slate", "title"}, {"Color", "color"}};
        for (const auto &[label, value] : kinds) type.addItem(label, value);
        if (existing && type.findData(existing->kind) < 0) type.addItem(existing->kind, existing->kind);
        QLineEdit name; name.setObjectName("sourceName"); name.setPlaceholderText("Optional source name");
        QComboBox device; device.setObjectName("sourceDevice"); device.setMinimumWidth(400);
        QLineEdit value; value.setObjectName("sourceValue");
        QPushButton browse("Choose file / color");
        QLabel help; help.setWordWrap(true);
        QCheckBox transparent("Transparent overlay background (up to 15 fps)"); transparent.setObjectName("browserTransparency");
        QSpinBox audioDelay; audioDelay.setObjectName("sourceAudioDelay"); audioDelay.setRange(0, 500); audioDelay.setSuffix(" ms");
        audioDelay.setToolTip("Delay this source's Program audio to match its picture. Headphone monitoring stays immediate.");
        form->addRow("Source type", &type); form->addRow("Name", &name); form->addRow("Device / window", &device);
        form->addRow("URL / file / text / color", &value); form->addRow(&browse); form->addRow(&transparent); form->addRow(&help);
        form->addRow("Audio delay", &audioDelay);
        if (editing) form->addRow(new QLabel("Source changes apply to every scene, including Program.", &dialog));
        auto populate = [&] {
            const auto kind = type.currentData().toString(); device.clear();
            transparent.setVisible(kind == "browser" || kind == "guest" || kind == "wireless" || kind == "chat");
            if (!existing) transparent.setChecked(kind == "browser" || kind == "chat");
            audioDelay.setEnabled(kind == "browser" || kind == "guest" || kind == "wireless" || kind == "window" || kind == "microphone" || kind == "desktop" || kind == "media");
            audioDelay.setValue(kind == "browser" || kind == "guest" || kind == "wireless" ? 60 : 0);
            value.clear(); value.setVisible(true); device.setVisible(true); browse.setVisible(kind == "image" || kind == "color" || kind == "media");
            if (kind == "window" || kind == "camera" || kind == "spout") {
                const auto devices = kind == "camera" ? core->listCameras() : (kind == "spout" ? core->listSpoutSenders() : core->listWindows());
                for (const auto &info : devices) device.addItem(QString::fromStdString(info.name), QString::fromStdString(info.id));
                value.setVisible(false);
                help.setText("Select a source. Window audio is captured when supported by Windows.");
            } else if (kind == "microphone") {
                device.addItem("Default microphone", "");
                for (const auto &info : core->listAudioInputDevices()) device.addItem(QString::fromStdString(info.name), QString::fromStdString(info.id));
                value.setVisible(false); help.setText("Microphones stay on across scenes by default. Use Mute or enable Scene in the audio mixer.");
            } else {
                device.setVisible(false);
                if (kind == "desktop") { value.setVisible(false); help.setText("Captures other applications, excluding Desktop Studio, its browsers and monitoring. Avoid adding the same game's individual audio twice."); }
                if (transparent.isVisible()) { value.setPlaceholderText("https://..."); help.setText("Overlay mode clears the page background automatically, keeping message and card styling. Turn it off for smooth browser video at 30 fps. Open / Invite allows interaction; Escape parks the browser."); }
                if (kind == "media") help.setText("Local video or audio file. Playback uses Windows media decoders; H.264/AAC MP4 is recommended. Use Playback controls after adding it.");
                if (kind == "image") help.setText("PNG, JPEG and other Qt-supported images. Transparent PNGs remain transparent.");
                if (kind == "title") { value.setText("Your title"); help.setText("A full-frame title card. Resize and position it on the canvas."); }
                if (kind == "color") { value.setText("#24466f"); help.setText("A solid color layer."); }
            }
        };
        QObject::connect(&type, &QComboBox::currentIndexChanged, &dialog, populate);
        QObject::connect(&browse, &QPushButton::clicked, &dialog, [&] {
            if (type.currentData() == "image" || type.currentData() == "media") {
                const auto file = QFileDialog::getOpenFileName(&dialog, "Choose file", {}, type.currentData() == "image" ? "Images (*.png *.jpg *.jpeg *.webp *.bmp)" : "Media (*.mp4 *.m4v *.mov *.wmv *.avi *.mp3 *.wav *.m4a);;All files (*)");
                if (!file.isEmpty()) value.setText(file);
            } else {
                const auto color = QColorDialog::getColor(QColor(value.text()), &dialog);
                if (color.isValid()) value.setText(color.name());
            }
        });
        QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel); form->addRow(&buttons);
        QObject::connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        QObject::connect(&buttons, &QDialogButtonBox::accepted, &dialog, [&] {
            const auto kind = type.currentData().toString();
            if (device.isVisible() && device.count() == 0) { help.setText("No source found. Connect a device or open a window first."); return; }
            if (transparent.isVisible() && !webUrl(value.text().trimmed())) { help.setText("Enter a complete http:// or https:// source URL."); return; }
            if (kind == "color" && !QColor(value.text()).isValid()) { help.setText("Enter a valid color, for example #24466f."); return; }
            if (kind == "media" && !QFileInfo(value.text()).isFile()) { help.setText("Choose an existing local media file."); return; }
            if (kind == "image" && !QImageReader(value.text()).canRead()) { help.setText("Choose a readable image file."); return; }
            auto item = existing ? existing : std::make_shared<Source>();
            const auto locator = device.isVisible() ? device.currentData().toString() : value.text().trimmed();
            // Labels and audio delay do not change the capture session. Keeping it
            // alive avoids dropping video/audio or rejoining a remote guest.
            const bool restart = existing && (reconnect || kind != item->kind ||
                locator != item->locator || transparent.isChecked() != item->transparent);
            if (restart && device.isVisible() && device.currentData(Qt::UserRole+1).toBool()) {
                help.setText("The saved device is unavailable. Select an available device to reconnect."); return;
            }
            if (restart) stopSource(item);
            item->kind = kind; item->transparent = transparent.isChecked();
            item->audioDelayMs = audioDelay.value();
            if (kind == "camera" && locator != item->locator) {
                item->cameraControls = {}; item->cameraWidth = Width; item->cameraHeight = Height; item->cameraFps = 30;
            }
            if (kind == "window" && locator != item->locator) { item->windowTitle.clear(); item->windowExecutable.clear(); }
            item->locator = locator;
            item->name = name.text().trimmed();
            if (item->name.isEmpty()) item->name = device.isVisible() ? device.currentText() : type.currentText();
            if (kind == "color") item->color = QColor(item->locator);
            if (!existing && (kind == "microphone" || kind == "desktop")) item->followsScene = false;
            if (!existing && kind == "microphone") item->monitored = false;
            if (existing) { if (restart) startSource(item); rebuild(); changed(); }
            else appendSource(item);
            dialog.accept();
        });
        if (existing) { type.setCurrentIndex(type.findData(existing->kind)); type.setEnabled(false); }
        populate();
        if (existing) {
            name.setText(existing->name); value.setText(existing->locator); transparent.setChecked(existing->transparent);
            audioDelay.setValue(existing->audioDelayMs);
            const int selectedDevice = device.findData(existing->locator);
            if (selectedDevice >= 0) device.setCurrentIndex(selectedDevice);
            else if (!device.isHidden()) {
                // Never substitute the first camera/mic/window when the saved
                // device is disconnected. Renaming it must retain its identity.
                device.insertItem(0, "Unavailable device (saved selection)", existing->locator);
                device.setItemData(0, true, Qt::UserRole+1);
                device.setItemData(0, "Choose another device to reconnect this source.", Qt::ToolTipRole);
                device.setCurrentIndex(0);
            }
        }
        dialog.exec();
    }

    void appendSource(const std::shared_ptr<Source> &item) {
        if (sources.size() >= 32) { message("This version supports up to 32 sources per show."); return; }
        { std::lock_guard lock(sourcesMutex); sources.push_back(item); }
        preview().layers.push_back({item->id}); selectedSource = item->id;
        startSource(item); rebuild(); changed();
    }

    void startSource(const std::shared_ptr<Source> &item) {
        item->error.clear(); item->lastFrame = 0; item->frames = 0;
        item->captureValid = std::make_shared<std::atomic<bool>>(true);
        const auto valid = item->captureValid;
        item->audioActive = item->hasAudio();
        const std::weak_ptr<Source> weak = item;
        auto frame = [weak, valid](video::CapturedFrame captured) {
            const auto item = weak.lock();
            if (!item || !*valid) return;
            auto image = takeFrameImage(std::move(captured)); if (image.isNull()) return;
            std::lock_guard lock(item->imageMutex); item->image = std::move(image); item->imageStraightAlpha = true;
            item->lastFrame = now100ns(); ++item->frames;
        };
        auto imageFrame = [weak, valid](QImage image) {
            const auto item = weak.lock(); if (!item || !*valid || image.isNull()) return;
            std::lock_guard lock(item->imageMutex); item->image = std::move(image); item->imageStraightAlpha = true;
            item->lastFrame = now100ns(); ++item->frames;
        };
        auto audioChunk = [weak, valid](audio::StreamChunk &&chunk) {
            const auto item = weak.lock(); if (!item || !*valid) return;
            std::lock_guard lock(item->audioMutex);
            auto normalized = audio::normalizeAudioForOpus(chunk, &item->resampler);
            item->samples.insert(item->samples.end(), normalized.begin(), normalized.end());
            // Keep at most 200 ms. A stalled UI must not accumulate delayed audio.
            while (item->samples.size() > 19200) item->samples.pop_front();
        };
        if (item->kind == "window" || item->kind == "camera" || item->kind == "spout" || item->kind == "microphone" || item->kind == "desktop") {
            struct Captures {
                std::unique_ptr<IsolatedSource> window;
                std::unique_ptr<IsolatedSource> camera;
                std::unique_ptr<IsolatedSource> spout;
                std::unique_ptr<IsolatedSource> audio;
                QString error, locator, title, executable;
            };
            auto result = std::make_shared<Captures>();
            const auto kind = item->kind, locator = item->locator, title = item->windowTitle, executable = item->windowExecutable;
            const auto cleanup = item->cleanup; const auto completion = std::make_shared<std::promise<void>>();
            item->cleanup = completion->get_future().share();
            const int width = Width, height = Height, fps = canvasFps;
            const int cameraWidth = item->cameraWidth, cameraHeight = item->cameraHeight; const double cameraFps = item->cameraFps;
            item->starting = true; const auto guard = QPointer<MixerWidget>(q);
            QThreadPool::globalInstance()->start([this, guard, weak, valid, result, kind, locator, title, executable, cleanup, width, height, fps, cameraWidth, cameraHeight, cameraFps, imageFrame, audioChunk, completion]() mutable {
                if (cleanup.valid()) cleanup.wait();
                if (!*valid) { completion->set_value(); return; }
                auto audioResult = [&](const audio::CaptureResult &value) { if (!value.success) result->error = "Audio unavailable: "+QString::fromStdString(value.error); };
                if (kind == "camera") {
                    result->camera = std::make_unique<IsolatedSource>("camera"); result->camera->setImageCallback(imageFrame);
                    if (!result->camera->startCapture(locator.toStdString(), cameraWidth, cameraHeight, cameraFps)) result->error = QString::fromStdString(result->camera->lastError());
                } else if (kind == "window") {
                    result->window = std::make_unique<IsolatedSource>("window"); result->window->setImageCallback(imageFrame);
                    const auto windows = result->window->getWindows(); const video::WindowInfo *match = nullptr;
                    for (const auto &info : windows) if (QString::fromStdString(info.id) == locator &&
                        (executable.isEmpty() || QString::fromStdString(info.executableName) == executable) &&
                        (title.isEmpty() || QString::fromStdString(info.name) == title)) match = &info;
                    if (!match && !title.isEmpty() && !executable.isEmpty()) {
                        int matches = 0;
                        for (const auto &info : windows) if (QString::fromStdString(info.name) == title && QString::fromStdString(info.executableName) == executable) { match = &info; ++matches; }
                        if (matches != 1) match = nullptr;
                    }
                    if (!match || !result->window->startCapture(match->id, width, height, fps, true)) result->error = "Window unavailable. Waiting for the saved window; use Reconnect to select another.";
                    else {
                        result->locator = QString::fromStdString(match->id); result->title = QString::fromStdString(match->name); result->executable = QString::fromStdString(match->executableName);
                        result->audio = std::make_unique<IsolatedSource>("audio"); audioResult(result->audio->StartStreamCapture(match->processId, audioChunk));
                    }
                } else if (kind == "spout") {
                    result->spout = std::make_unique<IsolatedSource>("spout"); result->spout->setImageCallback(imageFrame);
                    if (!result->spout->startCapture(locator.toStdString(), width, height, fps)) result->error = "Spout sender unavailable. Waiting for it to return.";
                } else {
                    result->audio = std::make_unique<IsolatedSource>("audio");
                    if (kind == "desktop") audioResult(result->audio->StartStreamCapture(static_cast<uint32_t>(QCoreApplication::applicationPid()), audioChunk, true));
                    else audioResult(result->audio->StartInputDeviceStreamCapture(locator.toStdString(), audioChunk));
                }
                if (!*valid || !guard) { result.reset(); completion->set_value(); return; }
                QMetaObject::invokeMethod(guard, [this, weak, valid, result, completion]() mutable {
                    const auto item = weak.lock(); if (!item || !*valid || !running) {
                        QThreadPool::globalInstance()->start([result = std::move(result), completion]() mutable { result.reset(); completion->set_value(); }); return;
                    }
                    item->window = std::move(result->window); item->camera = std::move(result->camera); item->spout = std::move(result->spout); item->audio = std::move(result->audio);
                    item->starting = false; item->error = result->error;
                    if (!result->locator.isEmpty()) { item->locator = result->locator; item->windowTitle = result->title; item->windowExecutable = result->executable; }
                    if (item->error.isEmpty()) item->retryAttempts = 0;
                    else { item->retryAttempts = std::min(5, item->retryAttempts+1); message(item->name+": "+item->error); }
                    item->retryAt = now100ns()+int64_t(std::min(30, 1 << item->retryAttempts))*10000000;
                    if (item->camera && item->camera->isCapturing()) for (auto it = item->cameraControls.begin(); it != item->cameraControls.end(); ++it) {
                        const auto setting = it.value().toObject(); applyCameraControl(item, it.key(), setting["value"].toInt(), setting["auto"].toBool(), {}, false);
                    }
                    completion->set_value(); changed();
                }, Qt::QueuedConnection);
            });
        } else if (item->kind == "media") {
            item->media = std::make_unique<IsolatedSource>("media"); item->media->setLoop(item->mediaLoop);
            item->media->setImageCallback(imageFrame);
            item->media->setPaused(item->mediaPaused); item->media->start(item->locator, frame, audioChunk);
        } else if (item->kind == "browser" || item->kind == "guest" || item->kind == "wireless" || item->kind == "chat") {
            if (!webUrl(item->locator)) { item->error = "Invalid browser URL."; return; }
            item->browser = new BrowserSource(item->id, QUrl(item->locator), true, item->transparent);
            QObject::connect(item->browser, &BrowserSource::restartRequired, q, [this, weak, valid] {
                if (auto item = weak.lock(); item && *valid && running) { stopSource(item); startSource(item); }
            }, Qt::QueuedConnection);
            QObject::connect(item->browser, &BrowserSource::recovered, q, [weak] { if (auto item = weak.lock()) item->error.clear(); });
            QObject::connect(item->browser, &BrowserSource::frameReady, q, [weak](const QImage &image) {
                if (auto item = weak.lock()) { std::lock_guard lock(item->imageMutex); item->image = image; item->lastFrame = now100ns(); ++item->frames; }
            });
            QObject::connect(item->browser, &BrowserSource::errorOccurred, q, [this, weak](const QString &message) {
                if (auto item = weak.lock()) {
                    item->error = message;
                    { std::lock_guard lock(item->imageMutex); item->image = {}; }
                    this->message(item->name+": "+message);
                }
            });
            QObject::connect(item->browser, &BrowserSource::audioReady, q, [weak, audioChunk](const QByteArray &pcm, int rate) {
                const auto item = weak.lock(); if (!item || !item->hasAudio()) return;
                audio::StreamChunk chunk; chunk.sampleRate = rate; chunk.captureTime100ns = now100ns();
                chunk.samples.resize(pcm.size()/2);
                for (qsizetype i = 0; i < pcm.size()/2; ++i) chunk.samples[i] = qFromLittleEndian<qint16>(pcm.constData()+i*2)/32768.0f;
                audioChunk(std::move(chunk));
            }, Qt::DirectConnection); // audioChunk owns its lock and checks capture validity.
        } else {
            QImage image;
            if (item->kind == "image") {
                QImageReader reader(item->locator); reader.setAutoTransform(true);
                if (reader.size().width() > 8192 || reader.size().height() > 8192) reader.setScaledSize(reader.size().scaled(Width*2, Height*2, Qt::KeepAspectRatio));
                image = reader.read(); if (image.isNull()) item->error = "Image unavailable: " + reader.errorString();
            } else {
                image = QImage(Width, Height, QImage::Format_ARGB32_Premultiplied); image.fill(item->color);
                if (item->kind == "title") {
                    QPainter painter(&image); painter.setRenderHint(QPainter::TextAntialiasing);
                    painter.setPen(Qt::white); QFont font("Segoe UI", 48, QFont::DemiBold); painter.setFont(font);
                    painter.drawText(image.rect().adjusted(60, 60, -60, -60), Qt::AlignCenter | Qt::TextWordWrap, item->locator);
                }
            }
            std::lock_guard lock(item->imageMutex); item->image = image;
        }
        if (!item->error.isEmpty()) message(item->name+": "+item->error);
    }

    void stopSource(const std::shared_ptr<Source> &item) {
        item->audioActive = false;
        ++item->cameraGeneration;
        *item->captureValid = false; item->starting = false;
        struct Retired {
            std::unique_ptr<IsolatedSource> window;
            std::unique_ptr<IsolatedSource> camera;
            std::unique_ptr<IsolatedSource> spout;
            std::unique_ptr<IsolatedSource> audio;
            std::unique_ptr<IsolatedSource> media;
        };
        auto retired = std::make_shared<Retired>();
        retired->window = std::move(item->window); retired->camera = std::move(item->camera);
        retired->spout = std::move(item->spout); retired->audio = std::move(item->audio); retired->media = std::move(item->media);
        auto promise = std::make_shared<std::promise<void>>(); const auto prior = item->cleanup;
        item->cleanup = promise->get_future().share();
        QThreadPool::globalInstance()->start([retired, promise, prior]() mutable { if (prior.valid()) prior.wait(); retired.reset(); promise->set_value(); });
        delete item->browser.data();
        { std::lock_guard lock(item->audioMutex); item->samples.clear(); item->delayedSamples.clear(); item->appliedDelayMs = -1; item->resampler = {}; }
        { std::lock_guard lock(item->imageMutex); item->image = {}; }
        item->error.clear(); item->peak = 0;
    }

    void removeSource() {
        auto item = source(selectedSource); if (!item) return;
        const bool usedOnProgram = std::any_of(program.layers.begin(), program.layers.end(), [&](const auto &layer) { return layer.source == item->id && layer.visible && item->admitted; });
        if (studioMode && (usedOnProgram || (fading && std::any_of(previous.layers.begin(), previous.layers.end(), [&](const auto &layer) { return layer.source == item->id && layer.visible && item->admitted; })))) {
            message("Take a scene without this source before removing it from the show."); return;
        }
        for (auto &scene : scenes) std::erase_if(scene.layers, [&](const auto &layer) { return layer.source == item->id; });
        { std::lock_guard lock(sourcesMutex); std::erase(sources, item); }
        // Destroy capture owners on the UI thread, including WebView2's apartment.
        item->onProgram = false;
        if (auto output = item->guestReturn.exchange({})) QThreadPool::globalInstance()->start([output] { output->stop(); });
        stopSource(item);
        selectedSource.clear(); rebuild(); changed();
    }

    void refreshGuestReturns() {
        for (const auto &item : sources) {
            const bool enable = guestReturnEnabled->isChecked() && !item->returnId.isEmpty();
            if (enable && !item->guestReturn.load()) item->guestReturn.store(std::make_shared<GuestReturn>(item->returnId, roomPassword, room, Width, Height));
            if (!enable) if (auto previous = item->guestReturn.exchange({})) QThreadPool::globalInstance()->start([previous] { previous->stop(); });
        }
    }
    void addInvite(bool cameraOnly) {
        bool ok; const auto name = QInputDialog::getText(q, cameraOnly ? "Wireless camera" : "Invite guest", "Source name", QLineEdit::Normal,
            cameraOnly ? "Wireless camera" : "Guest", &ok);
        if (!ok || name.trimmed().isEmpty()) return;
        auto item = std::make_shared<Source>(); item->kind = cameraOnly ? "wireless" : "guest"; item->name = name.trimmed();
        item->audioDelayMs = 60; item->admitted = false; item->returnId = "return"+newId().remove('-').left(24);
        const auto stream = "mix" + newId().remove('-'); const auto password = roomPassword;
        QUrl view("https://vdo.ninja/"); QUrlQuery viewQuery;
        viewQuery.addQueryItem("view", stream); viewQuery.addQueryItem("password", password);
        viewQuery.addQueryItem("room", room);
        viewQuery.addQueryItem("solo", ""); viewQuery.addQueryItem("cleanoutput", ""); view.setQuery(viewQuery); item->locator = view.toString(QUrl::FullyEncoded);
        QUrl push("https://vdo.ninja/"); QUrlQuery pushQuery;
        pushQuery.addQueryItem("push", stream); pushQuery.addQueryItem("password", password); pushQuery.addQueryItem("label", item->name);
        pushQuery.addQueryItem("room", room); pushQuery.addQueryItem("view", item->returnId);
        // A guest must publish as well as view; solo turns VDO.Ninja into a receive-only scene.
        if (cameraOnly) pushQuery.addQueryItem("rear", "");
        push.setQuery(pushQuery); item->invite = push.toString(QUrl::FullyEncoded);
        appendSource(item); guestReturnEnabled->setChecked(true); refreshGuestReturns(); showInvite(item);
    }
    void openTalkback() {
        if (!talkback) {
            QUrl url("https://vdo.ninja/"); QUrlQuery query;
            query.addQueryItem("director", room); query.addQueryItem("password", roomPassword);
            query.addQueryItem("vd", "0"); query.addQueryItem("muted", ""); query.addQueryItem("deaf", "");
            query.addQueryItem("label", "Studio talkback"); url.setQuery(query);
            talkback = new BrowserSource("talkback-"+room, url, false, false, true);
            talkback->setWindowTitle("Desktop Studio - Guest talkback (use headphones)");
            QObject::connect(talkback, &BrowserSource::errorOccurred, q, [this](const QString &text) { message(text); });
        }
        talkback->interact();
        message("Talkback: join with your microphone, then use the room's talk controls. It starts muted and deaf; monitor guests in Studio to avoid doubling audio.");
    }
    void showInvite(const std::shared_ptr<Source> &item) {
        QDialog dialog(q); dialog.setWindowTitle(item->name+" - invite"); auto *layout = new QVBoxLayout(&dialog);
        auto *help = new QLabel("Send this private link to the guest or open it on your phone.\nThey choose their camera and microphone, then join. Use headphones.\nPrivate talkback sends your microphone inputs only to guests.\nGuests join backstage. Select their source and choose Admit / Backstage when ready. Program return sends the show with their own audio removed.", &dialog); layout->addWidget(help);
        auto *url = new QLineEdit(item->invite, &dialog); url->setReadOnly(true); url->setMinimumWidth(620); url->setObjectName("inviteUrl"); layout->addWidget(url);
        auto *buttons = new QHBoxLayout;
        auto *copy = button("Copy invite link", "copyInvite", buttons, &dialog);
        auto *open = button("Open invite", "openInvite", buttons, &dialog);
        auto *inspect = button("Interact with receiver", "inspectReceiver", buttons, &dialog);
        auto *close = button("Done", "closeInvite", buttons, &dialog); layout->addLayout(buttons);
        QObject::connect(copy, &QPushButton::clicked, &dialog, [url, copy] { QApplication::clipboard()->setText(url->text()); copy->setText("Copied"); });
        QObject::connect(open, &QPushButton::clicked, &dialog, [item] { QDesktopServices::openUrl(QUrl(item->invite)); });
        QObject::connect(inspect, &QPushButton::clicked, &dialog, [item] { if (item->browser) item->browser->interact(); });
        QObject::connect(close, &QPushButton::clicked, &dialog, &QDialog::accept); dialog.exec();
    }
    void addChat() {
        bool ok; const auto value = QInputDialog::getText(q, "Social Stream Ninja", "Paste your overlay URL, or enter your Social Stream session ID.\nConnect your social platforms in Social Stream Ninja first.", QLineEdit::Normal, {}, &ok).trimmed();
        if (!ok || value.isEmpty()) return;
        auto item = std::make_shared<Source>(); item->name = "Social chat"; item->kind = "chat";
        item->transparent = true;
        if (webUrl(value)) item->locator = value;
        else {
            if (value.contains('/') || value.size() > 128) { message("Enter a session ID or a complete http(s) overlay URL."); return; }
            QUrl url("https://socialstream.ninja/featured.html"); QUrlQuery query; query.addQueryItem("session", value); url.setQuery(query);
            item->locator = url.toString(QUrl::FullyEncoded);
        }
        appendSource(item);
        message("Chat overlay added. Select it and choose Open / Invite to open the chat dock and feature messages; use Studio mode if you want to stage it before taking it live.");
    }

    void applyLayout(const QString &name) {
        std::vector<Layer*> layers;
        for (auto &layer : preview().layers) {
            auto item = source(layer.source);
            if (layer.visible && item && item->hasVideo() && !layer.locked) layers.push_back(&layer);
        }
        if (layers.empty()) return;
        if (name == "Full") { if (auto *layer = selectedLayer(); layer && !layer->locked) layer->rect = {0, 0, 1, 1}; }
        else if (name == "Side by side") {
            for (size_t i = 0; i < layers.size(); ++i) layers[i]->rect = {static_cast<double>(i)/layers.size(), 0, 1.0/layers.size(), 1};
        } else {
            layers[0]->rect = {0, 0, 1, 1};
            for (size_t i = 1; i < layers.size(); ++i) layers[i]->rect = {0.68, 0.66, 0.30, 0.30};
        }
        syncInspector(); changed();
    }
    void moveLayer(int delta) {
        auto &layers = preview().layers;
        for (int i = 0; i < static_cast<int>(layers.size()); ++i) if (layers[i].source == selectedSource && !layers[i].locked) {
            const int other = i+delta;
            if (other >= 0 && other < static_cast<int>(layers.size())) std::swap(layers[i], layers[other]);
            changed(); break;
        }
    }
    void takeScene(bool fade) {
        previous = program; program = preview(); fading = fade;
        if (fade) transition.restart();
        updateAudioRouting(); changed(); message("Program: " + program.name);
    }
    void updateAudioRouting() {
        for (auto &item : sources) item->onProgram = std::any_of(program.layers.begin(), program.layers.end(),
            [&](const auto &layer) { return layer.source == item->id && layer.visible && item->admitted; });
    }
    QImage compose(const Scene &scene, size_t slot) {
        Profile::Scope profile(Profile::Compose);
        std::vector<CompositionKey> keys;
        std::vector<QImage> frames;
        keys.reserve(scene.layers.size()); frames.reserve(scene.layers.size());
        for (const auto &layer : scene.layers) {
            if (!layer.visible || layer.opacity <= 0) continue;
            auto item = source(layer.source); if (!item || !item->hasVideo() || (!item->admitted && !(studioMode && slot == 0))) continue;
            QImage frame; bool straightAlpha;
            { std::lock_guard lock(item->imageMutex); frame = item->image; straightAlpha = item->imageStraightAlpha; }
            if (frame.isNull()) continue;
            const QRectF bounds(layer.rect.x()*Width, layer.rect.y()*Height, layer.rect.width()*Width, layer.rect.height()*Height);
            QRectF crop(layer.crop.x()*frame.width(), layer.crop.y()*frame.height(), layer.crop.width()*frame.width(), layer.crop.height()*frame.height());
            crop = crop.intersected(frame.rect());
            QSizeF fitted(crop.size());
            if (layer.fit == "fill") {
                const double ratio = bounds.width()/bounds.height();
                if (crop.width()/crop.height() > ratio) { const auto width = crop.height()*ratio; crop.setX(crop.x()+(crop.width()-width)/2); crop.setWidth(width); }
                else { const auto height = crop.width()/ratio; crop.setY(crop.y()+(crop.height()-height)/2); crop.setHeight(height); }
                fitted = bounds.size();
            } else if (layer.fit == "stretch") fitted = bounds.size();
            else fitted.scale(bounds.size(), Qt::KeepAspectRatio);
            const QRectF target(bounds.center().x()-fitted.width()/2, bounds.center().y()-fitted.height()/2, fitted.width(), fitted.height());
            keys.push_back({frame.cacheKey(), target, layer.opacity, straightAlpha, crop}); frames.push_back(std::move(frame));
        }
        auto &cached = compositions[slot];
        for (const auto &candidate : compositions) {
            if (!candidate.image.isNull() && candidate.image.size() == QSize(Width, Height) && candidate.background == scene.background && candidate.layers == keys) {
                if (&cached != &candidate) cached = candidate;
                return cached.image;
            }
        }
        // An unscaled, opaque source already is the complete scene. Its const
        // capture buffer remains shared until the last image reference is gone.
        if (keys.size() == 1 && keys[0].opacity == 1.0 &&
            keys[0].target == QRectF(0, 0, Width, Height) &&
            keys[0].crop == QRectF(0,0,Width,Height) && frames[0].size() == QSize(Width, Height) && frames[0].format() == QImage::Format_RGB32) {
            cached = {scene.background, std::move(keys), frames[0]};
            return cached.image;
        }
        if (renderer->currentIndex() == 0) {
            std::vector<GpuLayer> layers; layers.reserve(keys.size());
            for (size_t i = 0; i < keys.size(); ++i) {
                // Retain both opacity metadata and the cache key. Captured
                // RGB32 is opaque; wrapping it as ARGB32 needlessly converts
                // every frame and invalidates the GPU bitmap cache.
                layers.push_back({frames[i], keys[i].target, keys[i].crop, float(keys[i].opacity)});
            }
            auto image = gpu[slot].compose({Width, Height}, scene.background, layers);
            if (!image.isNull()) { cached = {scene.background, std::move(keys), std::move(image)}; return cached.image; }
            if (!gpu[slot].error().isEmpty()) message(gpu[slot].error());
        }
        QImage image(Width, Height, scene.background.alpha() == 255 ? QImage::Format_RGB32 : QImage::Format_ARGB32_Premultiplied);
        image.fill(scene.background);
        QPainter painter(&image); painter.setRenderHint(QPainter::SmoothPixmapTransform);
        for (size_t i = 0; i < keys.size(); ++i) {
            painter.setOpacity(keys[i].opacity);
            // Preserve Qt's original straight-alpha rounding for captured BGRA
            // under transforms/opacity. frames keeps this const view alive.
            const auto &source = frames[i];
            const QImage pixels = keys[i].straightAlpha && source.format() == QImage::Format_RGB32
                ? QImage(source.constBits(), source.width(), source.height(), source.bytesPerLine(), QImage::Format_ARGB32)
                : source;
            painter.drawImage(keys[i].target, pixels, keys[i].crop);
        }
        painter.end();
        cached = {scene.background, std::move(keys), std::move(image)};
        return cached.image;
    }
    void render() {
        const int64_t tick = now100ns();
        const int64_t period = 10000000/canvasFps;
        if (!nextRender || tick-nextRender > period*2) nextRender = tick;
        nextRender += period;
        videoTimer.setInterval(std::max(1, int((nextRender-tick+9999)/10000)));
        recordOutput->setText(recorder.active() ? "Stop recording" : "Record...");
        recordOutput->setAccessibleName(recordOutput->text());
        recordingStatus->setText(recorder.status()); recordingStatus->setVisible(!recorder.status().isEmpty());
        canvasFormat->setEnabled(!core->isLive() && !recorder.busy() && !guestReturnEnabled->isChecked() && !virtualCamera->isChecked() && !spoutOutput->isChecked());
        recordOutput->setEnabled(!recorder.busy() || recorder.active());
        if (studioMode && previewView->isVisible()) {
            const auto item = source(selectedSource);
            const auto selection = item && item->hasVideo() && selectedLayer() ? selectedLayer()->rect : QRectF();
            previewView->present(compose(preview(), 0), selection);
        }
        QImage frame = compose(program, 1);
        if (fading) {
            const double fraction = std::min(1.0, transition.elapsed()/400.0);
            QImage old = compose(previous, 2); QPainter painter(&old); painter.setOpacity(fraction); painter.drawImage(0, 0, frame); painter.end(); frame = old;
            if (fraction >= 1) fading = false;
        }
        if (studioMode) programView->present(frame);
        else if (previewView->isVisible()) {
            const auto item = source(selectedSource);
            const auto layer = selectedLayer();
            const auto selection = item && item->hasVideo() && layer && layer->visible && layer->opacity > 0 ? layer->rect : QRectF();
            previewView->present(frame, selection);
        }
        if (outputView) outputView->present(frame);
        recorder.submit(frame, tick);
        localOutputs.submit(frame, tick);
        for (const auto &item : sources) if (auto output = item->guestReturn.load()) output->video(frame, tick);
        const bool publishingProgram = core->isLive() && core->getSourceHealth().mode == app::VideoSourceMode::Mixer;
        previewTitle->setText(QString(studioMode ? "PREVIEW  /  " : publishingProgram ? "LIVE OUTPUT  /  " : "OUTPUT  /  ") + preview().name);
        programTitle->setText(QString(publishingProgram ? "LIVE OUTPUT  /  " : "PROGRAM  /  ") + program.name);
        if (publishingProgram) {
            Profile::Scope profile(Profile::MixerSubmit);
            video::CapturedFrame captured;
            captured.width = Width; captured.height = Height; captured.stride = frame.bytesPerLine(); captured.timestamp = now100ns();
            captured.opaque = !frame.hasAlphaChannel();
            captured.data.assign(frame.constBits(), frame.constBits()+frame.sizeInBytes()); core->submitMixerVideo(std::move(captured));
        }
        for (const auto &[weak, meter] : meters) if (auto item = weak.lock()) meter->setLevel(item->peak);
        masterMeter->setLevel(masterPeak);
        Profile::instance().flush();
        if (healthClock.elapsed() >= 1000) {
            const auto elapsed = healthClock.restart();
            const auto localError = localOutputs.status();
            if (!localError.isEmpty()) message(localError);
            for (const auto &candidate : sources) {
                if (candidate->starting || now100ns() < candidate->retryAt) continue;
                const bool failed = (candidate->camera && !candidate->camera->isCapturing()) ||
                    (candidate->audio && !candidate->audio->IsCapturing()) ||
                    (candidate->window && !candidate->window->isCapturing()) ||
                    (candidate->spout && !candidate->spout->isCapturing()) ||
                    (candidate->media && candidate->media->workerFailed());
                if (failed) { stopSource(candidate); startSource(candidate); }
            }
            if (auto item = source(selectedSource)) {
                const auto count = item->frames.load();
                const double fps = healthSource == item->id && count >= lastFrameCount ? (count-lastFrameCount)*1000.0/elapsed : 0;
                healthSource = item->id; lastFrameCount = count;
                QString text = item->starting ? "Connecting..." : item->error;
                if (text.isEmpty()) {
                    if (item->kind == "microphone" || item->kind == "desktop") text = "Audio capture active";
                    else if (item->kind == "image" || item->kind == "title" || item->kind == "color") text = "Ready";
                    else text = QString("%1 fps received").arg(fps, 0, 'f', 1);
                    if (item->camera) {
                        if (!item->camera->isCapturing()) text = QString::fromStdString(item->camera->lastError());
                        else {
                            const auto format = item->camera->settings().activeFormat;
                            text += QString(" / %1x%2 @ %3 fps").arg(format.width).arg(format.height).arg(format.fps, 0, 'g', 5);
                        }
                    }
                    if (item->media) text = !item->media->error().isEmpty() ? item->media->error() : item->media->ended() ? "Playback finished" : item->mediaPaused ? "Paused" : text;
                    if (item->browser) text += " / " + item->browser->captureDescription();
                }
                if (auto output = item->guestReturn.load()) text += " / "+output->status();
                if (!item->admitted) text += " / BACKSTAGE";
                sourceHealth->setText(item->name+": "+text);
            } else sourceHealth->setText("Select a source to inspect its status.");
        }
    }
    void mixAudio() {
        auto next = std::chrono::steady_clock::now();
        AudioMonitor monitor;
        int openedDevice = -999;
        while (running) {
            next += std::chrono::milliseconds(10);
            audio::StreamChunk chunk; chunk.samples.assign(960, 0); chunk.captureTime100ns = now100ns();
            std::vector<float> headphones(960, 0), hostTalk(960, 0);
            struct ReturnMix { std::shared_ptr<GuestReturn> output; std::vector<float> own; };
            std::vector<ReturnMix> returns;
            {
            std::lock_guard sourcesLock(sourcesMutex);
            for (const auto &item : sources) {
                if (!item->audioActive) continue;
                float peak = 0; const float sourceGain = item->muted ? 0 : item->gain.load();
                const bool talking = privateTalkback && item->kind == "microphone";
                const float gain = !talking && item->admitted && (!item->followsScene || item->onProgram) ? sourceGain : 0;
                const auto output = item->guestReturn.load();
                if (output) returns.push_back({output, std::vector<float>(960, 0)});
                const bool listen = item->monitored;
                std::lock_guard lock(item->audioMutex);
                const int delay = item->audioDelayMs.load();
                if (item->appliedDelayMs != delay) {
                    item->delayedSamples.assign(static_cast<size_t>(delay)*96, 0);
                    item->appliedDelayMs = delay;
                }
                const auto count = std::min(item->samples.size(), chunk.samples.size());
                std::array<float,960> processed{};
                for (size_t i = 0; i < count; ++i) { processed[i] = item->samples.front(); item->samples.pop_front(); }
                item->processor.process(processed, item->noiseSuppression, item->noiseGate, item->compression);
                for (size_t i = 0; i < chunk.samples.size(); ++i) {
                    const float sample = processed[i];
                    float delayed = sample;
                    if (delay) {
                        item->delayedSamples.push_back(sample);
                        delayed = item->delayedSamples.front(); item->delayedSamples.pop_front();
                    }
                    chunk.samples[i] += delayed*gain;
                    if (output) returns.back().own[i] = delayed*gain;
                    if (talking) hostTalk[i] += delayed*sourceGain;
                    peak = std::max(peak, std::abs(sample*sourceGain));
                    if (listen) headphones[i] += sample*sourceGain;
                }
                item->peak = std::max(peak, item->peak.load()*0.92f);
            }
            }
            for (auto &bus : returns) {
                float peak = 0;
                for (size_t i = 0; i < bus.own.size(); ++i) { bus.own[i] = (chunk.samples[i]-bus.own[i])*masterGain+hostTalk[i]; peak = std::max(peak, std::abs(bus.own[i])); }
                if (peak > .98f) for (auto &sample : bus.own) sample *= .98f/peak;
                bus.output->audio(std::move(bus.own), chunk.captureTime100ns);
            }
            float peak = 0;
            for (auto &value : chunk.samples) { value *= masterGain; peak = std::max(peak, std::abs(value)); }
            if (peak > 0.98f) for (auto &value : chunk.samples) value *= 0.98f/peak;
            masterPeak = std::min(0.98f, peak);
            if (monitoring) {
                const int device = monitorDeviceId.load();
                if (openedDevice != device) {
                    const auto error = monitor.open(device); openedDevice = device;
                    if (!error.isEmpty()) {
                        monitoring = false;
                        QMetaObject::invokeMethod(q, [this, error] { monitorEnable->setChecked(false); message(error); }, Qt::QueuedConnection);
                    }
                }
                const float level = masterGain.load()*monitorGain.load();
                for (auto &sample : headphones) sample = std::clamp(sample*level, -0.98f, 0.98f);
                if (monitoring) monitor.write(headphones);
            } else if (openedDevice != -999) { monitor.close(); openedDevice = -999; }
            recorder.audio(chunk.samples, chunk.captureTime100ns);
            if (core->isLive()) core->submitMixerAudio(std::move(chunk));
            if (next < std::chrono::steady_clock::now()-std::chrono::milliseconds(100)) next = std::chrono::steady_clock::now();
            std::this_thread::sleep_until(next);
        }
    }

    bool save(const QString &path) {
        QJsonArray jsonSources, jsonScenes;
        for (const auto &item : sources) jsonSources.append(QJsonObject{{"id", item->id}, {"name", item->name}, {"kind", item->kind},
            {"locator", item->locator}, {"invite", item->invite}, {"color", item->color.name(QColor::HexArgb)}, {"gain", item->gain.load()},
            {"noiseSuppression", item->noiseSuppression.load()}, {"noiseGate", item->noiseGate.load()}, {"compression", item->compression.load()}, {"muted", item->muted.load()}, {"followsScene", item->followsScene.load()}, {"monitored", item->monitored.load()}, {"transparent", item->transparent}, {"audioDelayMs", item->audioDelayMs.load()},
            {"returnId", item->returnId}, {"admitted", item->admitted.load()}, {"windowTitle", item->windowTitle}, {"windowExecutable", item->windowExecutable}, {"mediaLoop", item->mediaLoop}, {"mediaPaused", item->mediaPaused}, {"cameraWidth", item->cameraWidth}, {"cameraHeight", item->cameraHeight}, {"cameraFps", item->cameraFps}, {"cameraControls", item->cameraControls}});
        for (const auto &scene : scenes) {
            QJsonArray layers;
            for (const auto &layer : scene.layers) layers.append(QJsonObject{{"source", layer.source}, {"x", layer.rect.x()}, {"y", layer.rect.y()},
                {"w", layer.rect.width()}, {"h", layer.rect.height()}, {"visible", layer.visible}, {"opacity", layer.opacity}, {"locked", layer.locked}, {"fit", layer.fit},
                {"cropX", layer.crop.x()}, {"cropY", layer.crop.y()}, {"cropW", layer.crop.width()}, {"cropH", layer.crop.height()}});
            jsonScenes.append(QJsonObject{{"id", scene.id}, {"name", scene.name}, {"background", scene.background.name(QColor::HexArgb)}, {"layers", layers}});
        }
        QDir().mkpath(QFileInfo(path).absolutePath()); QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly)) { message("Could not save show: " + file.errorString()); return false; }
        const auto bytes = QJsonDocument(QJsonObject{{"version", 1}, {"sources", jsonSources}, {"scenes", jsonScenes},
            {"room", room}, {"roomPassword", roomPassword}, {"masterGain", masterGain.load()}, {"canvasFormat", canvasFormat->currentIndex()}, {"renderer", renderer->currentIndex()}}).toJson();
        if (file.write(bytes) != bytes.size() || !file.commit()) { message("Could not save show: " + file.errorString()); return false; }
        return true;
    }
    bool load(const QString &path) {
        QFile file(path); if (!file.exists()) return false;
        if (!file.open(QIODevice::ReadOnly) || file.size() > 2*1024*1024) { message("Cannot open this show file."); return false; }
        QJsonParseError error; const auto document = QJsonDocument::fromJson(file.readAll(), &error); const auto data = document.object();
        const auto sourceData = data["sources"].toArray(), sceneData = data["scenes"].toArray();
        if (error.error != QJsonParseError::NoError || data["version"].toInt() != 1 || sourceData.size() > 32 || sceneData.isEmpty() || sceneData.size() > 64) {
            message("Invalid show file or unsupported version."); return false;
        }
        std::vector<std::shared_ptr<Source>> incoming;
        std::vector<Scene> incomingScenes;
        QSet<QString> sourceIds, sceneIds;
        const QStringList kinds{"window", "camera", "microphone", "desktop", "spout", "browser", "guest", "wireless", "chat", "image", "title", "color", "media"};
        for (const auto &value : sourceData) {
            const auto obj = value.toObject(); auto item = std::make_shared<Source>();
            item->id = obj["id"].toString(); item->name = obj["name"].toString().left(256); item->kind = obj["kind"].toString();
            if (QUuid(item->id).isNull() || sourceIds.contains(item->id) || !kinds.contains(item->kind)) { message("Invalid or duplicate source in show."); return false; }
            sourceIds.insert(item->id);
            item->locator = obj["locator"].toString().left(16384); item->invite = obj["invite"].toString().left(16384);
            if (!item->invite.isEmpty() && !webUrl(item->invite)) { message("Invalid invite URL in show."); return false; }
            item->color = QColor(obj["color"].toString()); if (!item->color.isValid()) item->color = QColor("#183657");
            item->gain = static_cast<float>(std::clamp(obj["gain"].toDouble(1), 0.0, 2.0)); item->muted = obj["muted"].toBool(); item->followsScene = obj["followsScene"].toBool(true);
            item->noiseSuppression = obj["noiseSuppression"].toBool(); item->noiseGate = obj["noiseGate"].toBool(); item->compression = obj["compression"].toBool();
            item->monitored = obj["monitored"].toBool(item->kind != "microphone"); item->transparent = obj["transparent"].toBool(item->kind == "chat");
            const bool browserAudio = item->kind == "browser" || item->kind == "guest" || item->kind == "wireless";
            item->audioDelayMs = std::clamp(obj["audioDelayMs"].toInt(browserAudio ? 60 : 0), 0, 500);
            item->cameraWidth = std::clamp(obj["cameraWidth"].toInt(Width), 160, 7680);
            item->cameraHeight = std::clamp(obj["cameraHeight"].toInt(Height), 120, 4320);
            item->cameraFps = std::clamp(obj["cameraFps"].toDouble(30), 1.0, 240.0);
            item->cameraControls = obj["cameraControls"].toObject();
            item->returnId = obj["returnId"].toString().left(64); item->admitted = obj["admitted"].toBool(true);
            item->windowTitle = obj["windowTitle"].toString().left(512); item->windowExecutable = obj["windowExecutable"].toString().left(1024);
            item->mediaLoop = obj["mediaLoop"].toBool(true); item->mediaPaused = obj["mediaPaused"].toBool();
            if (item->cameraControls.size() > 32) { message("Too many saved camera controls."); return false; }
            incoming.push_back(item);
        }
        for (const auto &value : sceneData) {
            const auto obj = value.toObject(); Scene scene; scene.id = obj["id"].toString(); scene.name = obj["name"].toString().left(256);
            if (QUuid(scene.id).isNull() || sceneIds.contains(scene.id) || obj["layers"].toArray().size() > 32) { message("Invalid scene in show."); return false; }
            sceneIds.insert(scene.id); scene.background = QColor(obj["background"].toString()); if (!scene.background.isValid()) scene.background = Qt::black;
            QSet<QString> layerIds;
            for (const auto &entry : obj["layers"].toArray()) {
                const auto layer = entry.toObject(); const auto id = layer["source"].toString();
                if (!sourceIds.contains(id) || layerIds.contains(id)) { message("Invalid source reference in scene."); return false; }
                layerIds.insert(id);
                scene.layers.push_back({id, {std::clamp(layer["x"].toDouble(), 0.0, 1.0), std::clamp(layer["y"].toDouble(), 0.0, 1.0),
                    std::clamp(layer["w"].toDouble(1), 0.01, 1.0), std::clamp(layer["h"].toDouble(1), 0.01, 1.0)}, layer["visible"].toBool(true), std::clamp(layer["opacity"].toDouble(1), 0.0, 1.0), layer["locked"].toBool(),
                    {std::clamp(layer["cropX"].toDouble(), 0.0, .95), std::clamp(layer["cropY"].toDouble(), 0.0, .95),
                     std::clamp(layer["cropW"].toDouble(1), .01, 1.0), std::clamp(layer["cropH"].toDouble(1), .01, 1.0)},
                    layer["fit"].toString("fit")});
            }
            incomingScenes.push_back(scene);
        }
        loading = true;
        const int format = std::clamp(data["canvasFormat"].toInt(), 0, 4);
        canvasFormat->setCurrentIndex(format);
        renderer->setCurrentIndex(std::clamp(data["renderer"].toInt(1), 0, 1));
        const std::array<QSize,5> sizes{{{1280,720},{1920,1080},{1920,1080},{1280,720},{1080,1920}}};
        Width = sizes[format].width(); Height = sizes[format].height(); canvasFps = (format == 2 || format == 3) ? 60 : 30;
        compositions = {}; nextRender = 0;
        delete talkback.data();
        room = data["room"].toString(); roomPassword = data["roomPassword"].toString();
        if (room.size() < 16 || room.size() > 128 || roomPassword.size() < 16 || roomPassword.size() > 128) {
            room = "mix"+newId().remove('-').left(24); roomPassword = newId().remove('-');
        }
        // VDO.Ninja limits room names to 30 characters. Migrate older local shows.
        room = room.left(30);
        for (auto &item : incoming) for (auto *link : {&item->locator, &item->invite}) {
            if (!webUrl(*link)) continue;
            QUrl url(*link); QUrlQuery query(url); const auto name = query.queryItemValue("room");
            if (url.host() == "vdo.ninja" && name.size() > 30) {
                query.removeAllQueryItems("room"); query.addQueryItem("room", name.left(30)); url.setQuery(query); *link = url.toString(QUrl::FullyEncoded);
            }
        }
        if (auto *slider = q->findChild<QSlider*>("masterGain")) slider->setValue(qRound(std::clamp(data["masterGain"].toDouble(1), 0.0, 1.5)*100));
        std::vector<std::shared_ptr<Source>> old;
        { std::lock_guard lock(sourcesMutex); old.swap(sources); sources = std::move(incoming); }
        for (auto &item : old) { if (auto output = item->guestReturn.exchange({})) QThreadPool::globalInstance()->start([output] { output->stop(); }); stopSource(item); }
        scenes = std::move(incomingScenes);
        for (auto &item : sources) {
            if (item->kind == "window") item->locator.clear(); // Match stable executable/title, never a recycled HWND.
            startSource(item);
        }
        loading = false; refreshGuestReturns(); return true;
    }
};

MixerWidget::MixerWidget(app::VersusApp *core, QWidget *parent) : QWidget(parent), impl_(std::make_unique<Impl>(this, core)) {}
MixerWidget::~MixerWidget() = default;
void MixerWidget::shutdown() { impl_->shutdown(); }
QSize MixerWidget::canvasSize() const { return {impl_->Width, impl_->Height}; }
int MixerWidget::canvasFrameRate() const { return impl_->canvasFps; }
void MixerWidget::setOutputState(const QString &label, const QString &status, bool enabled, bool active) {
    impl_->startOutput->setText(label); impl_->startOutput->setAccessibleName(label);
    impl_->startOutput->setEnabled(enabled); impl_->recordOutput->setEnabled(!impl_->recorder.busy() || impl_->recorder.active());
    impl_->outputStatus->setText(active ? status : QString(status).replace("Select a window to capture", "Mixer ready"));
}
} // namespace versus::mixer
