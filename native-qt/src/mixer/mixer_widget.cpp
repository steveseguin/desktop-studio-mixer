#include "versus/mixer/mixer_widget.h"
#include "versus/mixer/browser_source.h"
#include "versus/mixer/audio_monitor.h"
#include "versus/mixer/program_recorder.h"
#include "versus/mixer/isolated_source.h"
#include "versus/mixer/gpu_compositor.h"
#include "versus/mixer/local_outputs.h"
#include "versus/mixer/audio_processor.h"
#include "versus/mixer/guest_return.h"
#include "versus/mixer/global_hotkeys.h"
#include "versus/mixer/show_files.h"
#include <QDateTime>
#include <QKeySequenceEdit>
#include <QProcess>
#include <QMenu>
#include <QToolButton>
#include "versus/video/video_encoder.h"
#include <QThreadPool>
#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrentRun>
#include "versus/mixer/audio_level_meter.h"
#include "versus/app/versus_app.h"
#include <QApplication>
#include <QAccessible>
#include "versus/ui/studio_preferences.h"
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
using ui::studioText;
namespace {
using Profile = app::detail::PerformanceTrace;
constexpr int Width = 1280, Height = 720;
struct CanvasFormat { int width, height, fps; };
// Indices are persisted in shows and used by the API; append new presets.
constexpr std::array<CanvasFormat, 8> CanvasFormats{{
    {1280,720,30}, {1920,1080,30}, {1920,1080,60}, {1280,720,60},
    {1080,1920,30}, {720,1280,30}, {720,1280,60}, {1080,1920,60}}};
QString newId() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
void announce(QWidget *widget, const QString &text) {
    if (!widget->isVisible() || !QAccessible::isActive() || text.isEmpty()) return;
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    QAccessibleAnnouncementEvent event(widget, text);
#else
    QAccessibleEvent event(widget, QAccessible::NameChanged);
#endif
    QAccessible::updateAccessibility(&event);
}
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
struct SceneAudioRoute {
    float from = 0, to = 0;
    int64_t start = 0;
    float at(int64_t time) const {
        return start ? from+(to-from)*std::clamp(float(time-start)/4000000.f, 0.f, 1.f) : to;
    }
};
struct Source {
    QString id = newId(), name, kind, locator, invite, error, browserStatusMessage;
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
    std::atomic<bool> programAudio{true};
    std::atomic<int> channelMode{0}; // stereo, mono mix, left to both, right to both
    std::atomic<std::shared_ptr<const SceneAudioRoute>> audioRoute{std::make_shared<const SceneAudioRoute>()};
    std::atomic<bool> monitored{true};
    std::atomic<bool> audioActive{false};
    bool transparent = false;
    bool preserveColorMetadata = false;
    int browserWidth = 1280, browserHeight = 720;
    bool mediaLoop = true, mediaPaused = false;
    bool mediaRestartOnTake = false;
    bool starting = false;
    int retryAttempts = 0;
    int64_t retryAt = 0;
    QString windowTitle, windowExecutable, returnId, returnMode = "video";
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
    std::deque<int64_t> browserRestarts;
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
    bool safeArea = false;
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
        if (safeArea) {
            const auto view = viewport(); painter.setPen(QPen(QColor(255,210,100,190),1,Qt::DashLine));
            painter.drawRect(view.adjusted(view.width()*.1,view.height()*.1,-view.width()*.1,-view.height()*.1));
        }
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
    auto *result = new QPushButton(studioText(text.toUtf8().constData()), parent);
    result->setObjectName(name);
    if (text.size() > 1) result->setAccessibleName(result->text());
    ui::accessibleWidget(result);
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
    struct RecordingPlan {
        QStringList audioIds;
        struct Iso { QString id; std::shared_ptr<ProgramRecorder> recorder; QSize size; };
        std::vector<Iso> isos;
    };
    QStringList recordAudioSources, recordIsoSources;
    std::atomic<std::shared_ptr<const RecordingPlan>> recordingPlan{std::make_shared<const RecordingPlan>()};
    LocalOutputs localOutputs;
    QCheckBox *spoutOutput = nullptr, *virtualCamera = nullptr, *guestReturnEnabled = nullptr;
    std::atomic<bool> privateTalkback{false};
    std::atomic<bool> privateBreak{false}, microphonesMuted{false}, heldTalkback{false};
    QImage breakSlate;
    QPushButton *breakButton = nullptr, *microphonesButton = nullptr;
    QPushButton *talkbackButton = nullptr;
    GlobalHotkeys globalHotkeys;
    bool hotkeysEnabled = false;
    QStringList hotkeyKeys;
    std::array<GpuCompositor,3> gpu;
    QComboBox *renderer = nullptr;
    QLabel *recordingStatus = nullptr;
    QString announcedRecordingStatus;
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
    QString programRenderer = "pending", compositorError;
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
    std::atomic<std::shared_ptr<const QString>> monitorDeviceKey{std::make_shared<const QString>("default")};
    bool monitorDevicesRefreshing = false;
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
    QHash<QString,qint64> backupTimes;
    bool collectionBusy = false;
    QString collectionStatus;
    std::shared_ptr<std::atomic<bool>> collectionCancelled = std::make_shared<std::atomic<bool>>(false);
    QPointer<QProcess> exportProcess;
    QString exportStatus, exportPath;

    Impl(MixerWidget *widget, app::VersusApp *appCore) : q(widget), core(appCore) {
        projectPath = showFileAccess().path();
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
        configureHotkeys(false);
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
        globalHotkeys.clear();
        if (!running.exchange(false)) return;
        *collectionCancelled = true;
        if (exportProcess) {
            QObject::disconnect(exportProcess,nullptr,q,nullptr);
            exportProcess->kill(); exportProcess->waitForFinished(1000);
            delete exportProcess.data(); exportProcess = nullptr;
        }
        videoTimer.stop(); saveTimer.stop(); save(projectPath);
        if (audioThread.joinable()) audioThread.join();
        recorder.stop();
        for (const auto &iso : recordingPlan.load()->isos) iso.recorder->stop();
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
    bool recordingBusy() const {
        if (recorder.busy()) return true;
        for (const auto &iso : recordingPlan.load()->isos) if (iso.recorder->busy()) return true;
        return false;
    }
    bool recordingActive() const {
        if (recorder.active()) return true;
        for (const auto &iso : recordingPlan.load()->isos) if (iso.recorder->active()) return true;
        return false;
    }
    void stopRecording() {
        recorder.requestStop(); for (const auto &iso : recordingPlan.load()->isos) iso.recorder->requestStop();
    }
    QString startRecording(const QString &path, const QStringList &audioIds, const QStringList &isoIds) {
        if (recordingBusy()) return "Recording is already active or finalizing.";
        if (audioIds.size() > 16 || isoIds.size() > 4) return "Choose at most 16 separate audio tracks and four separate video recordings.";
        auto plan = std::make_shared<RecordingPlan>(); QStringList names;
        for (const auto &id : audioIds) {
            auto item = source(id); if (!item || !item->hasAudio() || plan->audioIds.contains(id)) return "Choose unique existing audio sources.";
            plan->audioIds.append(id); names.append(item->name);
        }
        for (const auto &id : isoIds) {
            auto item = source(id); if (!item || !item->hasVideo()) return "Choose existing video sources for separate recordings.";
            if (std::any_of(plan->isos.begin(),plan->isos.end(),[&](const auto &iso) { return iso.id == id; })) return "Choose each video source once.";
            QSize size; { std::lock_guard lock(item->imageMutex); size = item->image.size(); }
            if (size.isEmpty()) size = QSize(Width,Height);
            size = QSize(std::max(2,size.width()/2*2),std::max(2,size.height()/2*2));
            plan->isos.push_back({id,std::make_shared<ProgramRecorder>(),size});
        }
        auto isoPath = [&](const QString &id) { return QFileInfo(path).absoluteDir().filePath(QFileInfo(path).completeBaseName()+"-source-"+id+".mkv"); };
        for (const auto &iso : plan->isos) if (QFileInfo::exists(isoPath(iso.id))) return "A separate source recording already exists. Choose a new recording name.";
        // Publish the immutable mapping before audio begins. Missing/removed
        // sources retain a silent track instead of changing track identity.
        recordingPlan.store(plan);
        if (!recorder.start(path,Width,Height,canvasFps,names)) return recorder.status();
        for (const auto &iso : plan->isos) iso.recorder->start(isoPath(iso.id),iso.size.width(),iso.size.height(),canvasFps);
        return {};
    }
    void recordingOptions() {
        if (recordingBusy()) { message("Stop recording before changing its tracks."); return; }
        QDialog dialog(q); dialog.setWindowTitle(studioText("Recording tracks")); auto *layout = new QVBoxLayout(&dialog);
        auto *note = new QLabel(studioText("Program is always recorded. Add up to 16 named audio tracks inside its MKV and four separate source MKVs. Selected sources record even off-screen. Mute, On air, Backstage and Private break still protect audio; Backstage and Private break hide separate video. Extra videos each require an encoder."),&dialog);
        note->setWordWrap(true); layout->addWidget(note);
        auto *scroll = new QScrollArea(&dialog); scroll->setWidgetResizable(true); scroll->setFrameShape(QFrame::StyledPanel);
        auto *contents = new QWidget; auto *form = new QFormLayout(contents); scroll->setWidget(contents); layout->addWidget(scroll);
        std::vector<std::pair<QString,QCheckBox*>> audio,video;
        for (const auto &item : sources) {
            if (item->hasAudio()) { auto *check = new QCheckBox(studioText("Separate audio track")); check->setObjectName("record_audio_"+item->id); check->setAccessibleName(item->name+" separate audio track"); check->setChecked(recordAudioSources.contains(item->id)); form->addRow(item->name,check); audio.emplace_back(item->id,check); }
            if (item->hasVideo()) { auto *check = new QCheckBox(studioText("Separate video file")); check->setObjectName("record_video_"+item->id); check->setAccessibleName(item->name+" separate video file"); check->setChecked(recordIsoSources.contains(item->id)); form->addRow(item->name,check); video.emplace_back(item->id,check); }
        }
        auto *feedback = new QLabel(&dialog); layout->addWidget(feedback);
        QDialogButtonBox buttons(QDialogButtonBox::Save|QDialogButtonBox::Cancel); layout->addWidget(&buttons);
        QObject::connect(&buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
        QObject::connect(&buttons,&QDialogButtonBox::accepted,&dialog,[&] {
            QStringList a,v; for (const auto &[id,check] : audio) if (check->isChecked()) a.append(id); for (const auto &[id,check] : video) if (check->isChecked()) v.append(id);
            if (a.size()>16 || v.size()>4) { feedback->setText(studioText("Choose at most 16 audio tracks and four video files.")); return; }
            recordAudioSources=a; recordIsoSources=v; changed(); dialog.accept();
        });
        dialog.resize(540,480); dialog.exec();
    }
    void record() {
        if (recordingActive()) { stopRecording(); return; }
        if (recordingBusy()) return;
        const auto path = QFileDialog::getSaveFileName(q, "Record Program", QStandardPaths::writableLocation(QStandardPaths::MoviesLocation)+"/Desktop Studio recording.mkv", "Matroska video (*.mkv)");
        if (path.isEmpty()) return;
        const auto error = startRecording(path.endsWith(".mkv", Qt::CaseInsensitive) ? path : path+".mkv",recordAudioSources,recordIsoSources);
        if (!error.isEmpty()) message(error);
    }
    QString exportRecording(const QString &input, const QString &output) {
        if (exportProcess) return "A recording export is already running.";
        if (recorder.busy() && QFileInfo(input).absoluteFilePath() == QFileInfo(recorder.path()).absoluteFilePath()) return "Stop recording before exporting this file.";
        for (const auto &iso : recordingPlan.load()->isos) if (iso.recorder->busy() && QFileInfo(input).absoluteFilePath() == QFileInfo(iso.recorder->path()).absoluteFilePath()) return "Stop recording before exporting this file.";
        if (!QFileInfo(input).isAbsolute() || !QFileInfo(input).isFile() || !input.endsWith(".mkv",Qt::CaseInsensitive)) return "Choose an existing absolute MKV recording path.";
        if (!QFileInfo(output).isAbsolute() || !output.endsWith(".mp4",Qt::CaseInsensitive) || QFileInfo::exists(output)) return "Choose a new absolute MP4 path; existing files are never overwritten.";
        const auto ffmpeg = QString::fromStdString(video::VideoEncoder::resolveFfmpegPath(""));
        if (ffmpeg.isEmpty()) return "FFmpeg is unavailable.";
        auto job = std::shared_ptr<void>(CreateJobObjectW(nullptr,nullptr),[](void *handle) { if (handle) CloseHandle(handle); });
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{}; limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (!job || !SetInformationJobObject(job.get(),JobObjectExtendedLimitInformation,&limits,sizeof(limits))) return "Could not isolate the MP4 export process.";
        auto *process = new QProcess(q); exportProcess = process; exportPath = output; exportStatus = "Exporting MP4...";
        process->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args) { args->flags |= CREATE_NO_WINDOW; });
        QObject::connect(process,&QProcess::started,q,[this,process,job] {
            const auto child = OpenProcess(PROCESS_SET_QUOTA|PROCESS_TERMINATE,FALSE,DWORD(process->processId()));
            const bool isolated = child && AssignProcessToJobObject(job.get(),child);
            if (child) CloseHandle(child);
            if (!isolated) { process->kill(); message("Could not isolate the MP4 export process. Original recording preserved."); }
        });
        process->setProcessChannelMode(QProcess::MergedChannels);
        QObject::connect(process, &QProcess::readyRead, q, [process] { process->readAll(); });
        QObject::connect(process, &QProcess::errorOccurred, q, [this,process](QProcess::ProcessError error) {
            if (error != QProcess::FailedToStart) return;
            exportStatus = "Could not start MP4 export. Original recording preserved."; message(exportStatus); exportProcess = nullptr; process->deleteLater();
        });
        QObject::connect(process, qOverload<int,QProcess::ExitStatus>(&QProcess::finished), q, [this,process](int code,QProcess::ExitStatus status) {
            exportStatus = code == 0 && status == QProcess::NormalExit ? "MP4 saved: "+QFileInfo(exportPath).fileName() : "MP4 export failed. Original recording preserved; choose a new filename to retry.";
            message(exportStatus); exportProcess = nullptr; process->deleteLater();
        });
        process->setProgram(ffmpeg); process->setArguments({"-nostdin","-hide_banner","-loglevel","error","-n","-i",input,"-map","0:v:0","-map","0:a?","-c","copy","-movflags","+faststart",output}); process->start();
        message(exportStatus); return {};
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
    void message(const QString &text) {
        if (status->text() == text) return;
        status->setText(text);
        announce(status, text);
    }

    QStringList hotkeyNames() const {
        QStringList names{"Private break", "Mute microphones", "Take / Cut", "Hold private talkback"};
        for (int n=1; n<=9; ++n) names << QString("Scene %1 (Preview in Studio mode)").arg(n);
        return names;
    }
    void configureHotkeys(bool dialog) {
        const auto names = hotkeyNames();
        if (hotkeyKeys.isEmpty()) {
            hotkeyKeys = {"Ctrl+Alt+B", "Ctrl+Alt+M", "Ctrl+Alt+Return", "Ctrl+Alt+T"};
            for (int n=1; n<=9; ++n) hotkeyKeys << QString("Ctrl+Alt+%1").arg(n);
            if (qApp->property("studioShow").toString().isEmpty()) {
                QSettings settings("VDO.Ninja", "Video Mixer");
                hotkeysEnabled = settings.value("mixer/globalHotkeys", false).toBool();
                const auto saved = settings.value("mixer/globalHotkeyKeys").toStringList();
                if (saved.size() == hotkeyKeys.size()) hotkeyKeys = saved;
            }
        }
        auto apply = [this, names]() {
            globalHotkeys.clear(); QStringList errors;
            if (hotkeysEnabled) for (int n=0; n<hotkeyKeys.size(); ++n) {
                const auto error = globalHotkeys.add(n, QKeySequence(hotkeyKeys[n]), [this,n](bool pressed) {
                    if (n == 3) { heldTalkback = pressed; return; }
                    if (!pressed || !running) return;
                    if (n == 0) breakButton->toggle();
                    else if (n == 1) microphonesButton->toggle();
                    else if (n == 2) { if (studioMode) takeScene(false); }
                    else if (n-4 < int(scenes.size())) sceneList->setCurrentRow(n-4);
                });
                if (!error.isEmpty()) errors << names[n]+": "+error;
            }
            // Apply all-or-none so a conflict cannot silently disable a safety key.
            if (!errors.isEmpty()) { globalHotkeys.clear(); hotkeysEnabled = false; }
            return errors.join('\n');
        };
        if (!dialog) { const auto error = apply(); if (!error.isEmpty()) message("Global hotkeys disabled: "+error); return; }
        QDialog window(q); window.setWindowTitle(studioText("Global hotkeys"));
        auto *form = new QFormLayout(&window);
        QCheckBox enabled(studioText("Enable shortcuts while other applications have focus")); enabled.setObjectName("globalHotkeysEnabled"); enabled.setChecked(hotkeysEnabled); form->addRow(&enabled);
        std::vector<QKeySequenceEdit*> editors;
        for (int n=0; n<names.size(); ++n) {
            auto *edit = new QKeySequenceEdit(QKeySequence(hotkeyKeys[n]), &window);
            edit->setMaximumSequenceLength(1); edit->setObjectName(QString("globalHotkey_%1").arg(n)); edit->setAccessibleName(names[n]);
            form->addRow(names[n], edit); editors.push_back(edit);
        }
        QLabel feedback; feedback.setWordWrap(true); form->addRow(&feedback);
        QDialogButtonBox buttons(QDialogButtonBox::Save|QDialogButtonBox::Cancel); form->addRow(&buttons);
        QObject::connect(&buttons, &QDialogButtonBox::rejected, &window, &QDialog::reject);
        QObject::connect(&buttons, &QDialogButtonBox::accepted, &window, [&] {
            hotkeysEnabled = enabled.isChecked();
            for (int n=0; n<int(editors.size()); ++n) hotkeyKeys[n] = editors[n]->keySequence().toString();
            const auto error = apply();
            if (qApp->property("studioShow").toString().isEmpty()) {
                QSettings settings("VDO.Ninja", "Video Mixer"); settings.setValue("mixer/globalHotkeys", hotkeysEnabled); settings.setValue("mixer/globalHotkeyKeys", hotkeyKeys);
            }
            if (!error.isEmpty()) { feedback.setText("Global hotkeys disabled. "+error); enabled.setChecked(false); return; }
            window.accept();
        });
        // Release registrations while entering new combinations.
        globalHotkeys.clear(); window.exec();
        const auto error = apply(); if (!error.isEmpty()) message("Global hotkeys disabled: "+error);
    }

    void buildUi() {
        q->setMinimumSize(1100, 620);
        q->setStyleSheet("QPushButton, QToolButton#exportShow { background:#192b3d; color:#eaf4ff; border:1px solid #304358; border-radius:4px; padding:8px; }"
            "QToolButton#exportShow { padding-right:20px; font-weight:700; } QToolButton#exportShow::menu-button { width:16px; border-left:1px solid #304358; }"
            "QPushButton:hover, QToolButton#exportShow:hover { background:#253c52; } QPushButton#takeScene, QPushButton#mixerOutput { background:#00c2ff; color:#06131a; font-weight:700; }"
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
        talkbackButton = talk;
        auto *output = button("Output settings", "mixerOutput", toolbar, q);
        QObject::connect(invite, &QPushButton::clicked, q, [this] { addInvite(false); });
        QObject::connect(wireless, &QPushButton::clicked, q, [this] { addInvite(true); });
        QObject::connect(chat, &QPushButton::clicked, q, [this] { addChat(); });
        talk->setText(studioText("Private talkback")); talk->setCheckable(true);
        talk->setToolTip(studioText("While enabled, microphone inputs go only to guests and are removed from Program. Add your microphone as a source first."));
        QObject::connect(talk, &QPushButton::toggled, q, [this](bool enabled) {
            privateTalkback = enabled;
            message(enabled ? "PRIVATE TALKBACK: microphone inputs are off air and sent only to guests. Click Private talkback again to return them to Program." : "Private talkback ended. Normal microphone routing restored.");
        });
        QObject::connect(output, &QPushButton::clicked, q, [this] { emit q->outputRequested(); });
        root->addLayout(toolbar);
        auto *outputBar = new QHBoxLayout;
        startOutput = button("Start output", "studioStartOutput", outputBar, q);
        recordOutput = button("Record...", "studioRecord", outputBar, q);
        breakButton = button("Private break", "privateBreak", outputBar, q); breakButton->setCheckable(true);
        breakButton->setToolTip(studioText("Show a Be right back slate and silence all Program audio. Recording and streaming continue. Admitted guests and host microphones can still converse privately. Click again to resume the selected Program."));
        QObject::connect(breakButton, &QPushButton::toggled, q, [this](bool enabled) {
            privateBreak = enabled; breakButton->setText(enabled ? "Resume show" : "Private break");
            breakButton->setAccessibleName(breakButton->text());
            message(enabled ? "PRIVATE BREAK: Program shows a slate and is silent. Guest conversation continues privately." : "Show resumed. Program picture and audio are on air.");
        });
        microphonesButton = button("Mute mics", "muteMicrophones", outputBar, q); microphonesButton->setCheckable(true);
        microphonesButton->setToolTip(studioText("Mute all local microphone inputs, including monitoring and guest returns. Individual track mute settings are preserved."));
        QObject::connect(microphonesButton, &QPushButton::toggled, q, [this](bool enabled) {
            microphonesMuted = enabled; microphonesButton->setText(enabled ? "Unmute mics" : "Mute mics");
            microphonesButton->setAccessibleName(microphonesButton->text());
        });
        auto *hotkeys = button("Hotkeys...", "globalHotkeys", outputBar, q);
        QObject::connect(hotkeys, &QPushButton::clicked, q, [this] { configureHotkeys(true); });
        auto *recordingFiles = button("Recordings", "recordingFiles", outputBar, q);
        auto *recordingMenu = new QMenu(recordingFiles); recordingFiles->setMenu(recordingMenu);
        recordingMenu->addAction(studioText("Recording tracks..."),q,[this] { recordingOptions(); });
        QObject::connect(recordingMenu->addAction(studioText("Open recording folder")), &QAction::triggered, q, [this] {
            QDesktopServices::openUrl(QUrl::fromLocalFile(recorder.path().isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::MoviesLocation) : QFileInfo(recorder.path()).absolutePath()));
        });
        QObject::connect(recordingMenu->addAction(studioText("Export recording as MP4...")), &QAction::triggered, q, [this] {
            const auto input = QFileDialog::getOpenFileName(q,"Choose recording to export",recorder.path(),"Matroska recording (*.mkv)"); if (input.isEmpty()) return;
            const auto output = QFileDialog::getSaveFileName(q,"Export MP4 (original recording preserved)",QFileInfo(input).absolutePath()+"/"+QFileInfo(input).completeBaseName()+".mp4","MP4 video (*.mp4)"); if (output.isEmpty()) return;
            const auto error = exportRecording(input,output.endsWith(".mp4",Qt::CaseInsensitive) ? output : output+".mp4"); if (!error.isEmpty()) message(error);
        });
        outputStatus = new QLabel(studioText("Configure a destination in Output settings, or record to a file."), q);
        outputStatus->setObjectName("studioOutputStatus"); outputStatus->setWordWrap(true);
        outputBar->addWidget(outputStatus, 1); root->addLayout(outputBar);
        QObject::connect(startOutput, &QPushButton::clicked, q, [this] { emit q->toggleOutputRequested(); });
        QObject::connect(recordOutput, &QPushButton::clicked, q, [this] { record(); });
        recordingStatus = new QLabel(q); recordingStatus->setObjectName("recordingStatus"); recordingStatus->setWordWrap(true); root->addWidget(recordingStatus);
        canvasFormat = new QComboBox(q); canvasFormat->setObjectName("canvasFormat");
        canvasFormat->setToolTip(studioText("Choose the show resolution and frame rate before starting outputs. Stop streaming, recording, guest returns and local outputs to change it."));
        for (const auto &format : CanvasFormats)
            canvasFormat->addItem(QString("%1x%2 / %3 fps%4").arg(format.width).arg(format.height).arg(format.fps)
                .arg(format.height > format.width ? studioText(" (Portrait)") : QString()));
        outputBar->addWidget(canvasFormat);
        auto *localBar = new QHBoxLayout;
        outputBar->removeWidget(hotkeys); outputBar->removeWidget(recordingFiles);
        spoutOutput = new QCheckBox(studioText("Spout output"), q); spoutOutput->setObjectName("spoutOutput"); localBar->addWidget(spoutOutput);
        virtualCamera = new QCheckBox(studioText("Virtual camera"), q); virtualCamera->setObjectName("virtualCamera"); localBar->addWidget(virtualCamera);
        auto *removeCamera = button("Unregister camera", "unregisterCamera", localBar, q);
        guestReturnEnabled = new QCheckBox(studioText("Guest Program return"), q); guestReturnEnabled->setObjectName("guestProgramReturn"); localBar->addWidget(guestReturnEnabled);
        QObject::connect(guestReturnEnabled, &QCheckBox::toggled, q, [this] { refreshGuestReturns(); changed(); });
        localBar->addStretch(); localBar->addWidget(hotkeys); localBar->addWidget(recordingFiles); root->addLayout(localBar);
        QObject::connect(spoutOutput, &QCheckBox::toggled, q, [this] { localOutputs.configure(spoutOutput->isChecked(), virtualCamera->isChecked(), canvasFps); });
        const auto registerCamera = [this,removeCamera](bool install) {
            virtualCamera->setEnabled(false); removeCamera->setEnabled(false);
            auto *watcher = new QFutureWatcher<QString>(q);
            QObject::connect(watcher,&QFutureWatcher<QString>::finished,q,[this,removeCamera,watcher,install] {
                const auto error = watcher->result(); watcher->deleteLater();
                virtualCamera->setEnabled(true); removeCamera->setEnabled(true);
                if (!error.isEmpty()) { message(error); const QSignalBlocker block(virtualCamera); virtualCamera->setChecked(false); }
                else message(install ? "Select Desktop Studio Virtual Camera in another app. Camera output carries video; select your microphone there separately." : "Virtual camera removed from this Windows account.");
                localOutputs.configure(spoutOutput->isChecked(),virtualCamera->isChecked(),canvasFps);
            });
            watcher->setFuture(QtConcurrent::run([install] { return LocalOutputs::registerCamera(install); }));
        };
        QObject::connect(virtualCamera, &QCheckBox::toggled, q, [this,registerCamera](bool on) {
            if (on) registerCamera(true);
            else { localOutputs.configure(spoutOutput->isChecked(),false,canvasFps); message("Virtual camera stopped."); }
        });
        QObject::connect(removeCamera, &QPushButton::clicked, q, [this,registerCamera] { virtualCamera->setChecked(false); registerCamera(false); });
        renderer = new QComboBox(q); renderer->setObjectName("compositorBackend"); renderer->addItems({"GPU composition", "CPU composition"}); outputBar->addWidget(renderer);
        // The default 720p mix is cheaper on CPU while outputs require readback.
        // Keep GPU available for workloads where its scaling/compositing wins.
        renderer->setCurrentIndex(1);
        QObject::connect(renderer, &QComboBox::currentIndexChanged, q, [this] { compositions = {}; changed(); });
        QObject::connect(canvasFormat, &QComboBox::currentIndexChanged, q, [this](int index) {
            if (loading || rebuilding) return;
            if (index < 0 || index >= int(CanvasFormats.size())) return;
            if (core->isLive() || recordingBusy()) { message("Stop streaming and recording before changing the canvas format."); return; }
            const auto &format = CanvasFormats[index];
            Width = format.width; Height = format.height; canvasFps = format.fps;
            compositions = {}; nextRender = 0; videoTimer.setInterval(1000/canvasFps);
            // Window/Spout workers negotiate their size and pacing at startup.
            // Retune existing sources too, including when moving from 30 to 60 fps.
            for (const auto &item : sources)
                if (item->kind == "title" || item->kind == "color" || item->kind == "window" || item->kind == "spout") { stopSource(item); startSource(item); }
            changed();
        });
        monitors = new QHBoxLayout;
        auto monitor = [&](const QString &name, Canvas *&view, QLabel *&title) {
            auto *panel = new QWidget(q);
            auto *box = new QVBoxLayout(panel); box->setContentsMargins(0, 0, 0, 0);
            title = new QLabel(name, q); title->setStyleSheet("font-weight:700; color:#9ab5e8");
            box->addWidget(title); view = new Canvas(q); view->setObjectName(name.toLower()+"Canvas");
            view->setAccessibleName(name+" canvas");
            view->setAccessibleDescription("Rendered scene. Select a source in the Sources list and use the position, size, crop and opacity controls to edit it with the keyboard.");
            box->addWidget(view, 1); monitors->addWidget(panel, 1);
            return panel;
        };
        monitor("Preview", previewView, previewTitle);
        programPanel = monitor("Program", programView, programTitle);
        root->addLayout(monitors, 4);
        auto *transport = new QHBoxLayout;
        auto *mode = new QCheckBox(studioText("Studio mode"), q); mode->setObjectName("studioMode");
        mode->setChecked(studioMode);
        mode->setToolTip(studioText("Stage scenes in Preview before taking them live. Turning Studio mode off takes Preview to output and enables direct editing."));
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
        shortcut(QKeySequence("Ctrl+Shift+L"), [this] {
            auto item = source(selectedSource);
            const bool track = item && item->hasAudio();
            const float peak = track ? item->peak.load() : masterPeak.load();
            const auto value = peak >= 1 ? QString("Clipping") : peak <= .001f ? QString("Silent, below -60 dBFS") : QString::number(20*std::log10(peak), 'f', 1)+" dBFS";
            const auto label = (track ? item->name : QString("Master"))+": "+value;
            // Repeating the shortcut must speak again even if the value is stable.
            announce(q, label);
        });
        shortcut(QKeySequence("Ctrl+Return"), [this] { if (studioMode) takeScene(false); });
        for (int i = 0; i < 9; ++i) shortcut(QKeySequence(QString("Ctrl+%1").arg(i+1)), [this, i] {
            if (i < int(scenes.size())) sceneList->setCurrentRow(i);
        });
        QObject::connect(mode, &QCheckBox::toggled, q, [this](bool enabled) {
            studioMode = enabled; draggingLayer = false;
            if (qApp->property("studioShow").toString().isEmpty())
                QSettings("VDO.Ninja", "Video Mixer").setValue("mixer/studioMode", enabled);
            changed(); updateModeUi(); rebuildSources();
            message(enabled ? "Studio mode: layout edits stay in Preview until you Take." : "Single canvas: the selected scene is on output. Edits apply immediately.");
        });
        takeButton = button("Take / Cut", "takeScene", transport, q);
        fadeButton = button("Fade (400 ms)", "fadeScene", transport, q);
        transport->addStretch();
        auto *popout = button("Program window", "programWindow", transport, q);
        auto *safeArea = new QCheckBox(studioText("Safe area"),q); safeArea->setObjectName("safeArea"); transport->addWidget(safeArea);
        safeArea->setToolTip(studioText("Show a generic 10% margin in the editing canvas only. Keep important text inside; platform controls may cover additional space. Guides are never recorded or streamed."));
        QObject::connect(safeArea,&QCheckBox::toggled,q,[this](bool enabled) { previewView->safeArea = enabled; previewView->update(); });
        auto *exportShow = new QToolButton(q); exportShow->setText(studioText("Save a copy...")); exportShow->setObjectName("exportShow");
        exportShow->setAccessibleName(studioText("Save a copy")); exportShow->setPopupMode(QToolButton::MenuButtonPopup); transport->addWidget(exportShow);
        auto *projectMenu = new QMenu(exportShow); exportShow->setMenu(projectMenu);
        projectMenu->addAction(studioText("Save As active show..."),q,[this] {
            const auto path = QFileDialog::getSaveFileName(q,"Save As active show",projectPath,"Mixer show (*.mixer.json)");
            if (!path.isEmpty()) saveAs(path.endsWith(".mixer.json",Qt::CaseInsensitive) ? path : path+".mixer.json");
        });
        projectMenu->addAction(studioText("Collect show and assets..."),q,[this] {
            const auto path = QFileDialog::getSaveFileName(q,"New folder for collected show",QFileInfo(projectPath).absolutePath()+"/Collected show");
            if (!path.isEmpty()) { const auto error = collect(path); if (!error.isEmpty()) message(error); }
        });
        projectMenu->addAction(studioText("Relink missing assets from folder..."),q,[this] {
            const auto path = QFileDialog::getExistingDirectory(q,"Folder containing missing assets");
            if (!path.isEmpty()) message(QString("Relinked %1 missing assets by filename.").arg(relink(path)));
        });
        exportShow->setToolTip(studioText("Export a show snapshot. Later edits continue autosaving in the current workspace; save another copy to update the exported file. Local assets inside the destination folder use relative paths."));
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
        QObject::connect(exportShow, &QToolButton::clicked, q, [this] {
            const auto path = QFileDialog::getSaveFileName(q, "Save show (includes source URLs and invite links)", {}, "Mixer show (*.mixer.json)");
            if (!path.isEmpty() && save(path)) message("Show copy saved. Later edits autosave in this workspace; save another copy to update that file.");
        });
        QObject::connect(importShow, &QPushButton::clicked, q, [this] {
            if (core->isLive() || recordingBusy() || virtualCamera->isChecked() || spoutOutput->isChecked()) { message("Stop streaming, recording and local outputs before opening another show."); return; }
            const auto path = QFileDialog::getOpenFileName(q, "Open show", {}, "Mixer show (*.mixer.json)");
            if (path.isEmpty()) return;
            auto lease = showFileAccess().reserve(path);
            if (!lease.allowed) { message("This show is open in another instance, or its folder cannot be locked."); return; }
            if (!save(projectPath)) return;
            if (load(path)) {
                activateProject(std::move(lease));
                selectedScene = 0; selectedSource.clear(); program = scenes[0]; fading = false;
                history.clear(); historyIndex = 0; rebuild(); updateAudioRouting(); changed();
            } else {
                // Source retries can replace the status line while the user reads
                // an import error. Keep this explicit action's result visible.
                QMessageBox::warning(q, studioText("Could not open show"),
                    status->text()+"\n\n"+studioText("Your current show has been kept."));
            }
        });
        root->addLayout(transport);

        panels = new QSplitter(Qt::Horizontal, q);
        auto *sceneBox = new QGroupBox(studioText("Scenes"), panels); auto *sceneLayout = new QVBoxLayout(sceneBox);
        sceneList = new QListWidget(sceneBox); sceneList->setObjectName("sceneList"); sceneLayout->addWidget(sceneList);
        auto *sceneButtons = new QHBoxLayout;
        auto *newScene = button("+", "addScene", sceneButtons, q);
        auto *duplicate = button("Duplicate", "duplicateScene", sceneButtons, q);
        auto *rename = button("Rename", "renameScene", sceneButtons, q);
        auto *removeScene = button("-", "removeScene", sceneButtons, q); sceneLayout->addLayout(sceneButtons);
        auto *background = new QPushButton(studioText("Background color"), q); sceneLayout->addWidget(background);
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

        sourceBox = new QGroupBox(studioText("Sources"), panels);
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
        admit->setToolTip(studioText("Admission enables guest conversation. With On air enabled, their voice can enter Program immediately, subject to Scene and Mute. Studio mode stages their picture for Take. Backstage removes picture and voice from Program."));
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
            message(item->admitted ? (studioMode ? (!item->followsScene && !item->muted && item->programAudio && !privateBreak
                ? "Guest admitted. Audio is on Program; Take when ready to show their picture."
                : "Guest admitted to Preview. Take when ready; audio follows its mixer controls.")
                : "Guest admitted. Picture and audio follow the scene and mixer controls.")
                : "Guest is backstage. Picture and audio are off air; Program return continues.");
        });
        auto *playback = button("Playback", "mediaPlayback", editButtons, q);
        QObject::connect(playback, &QPushButton::clicked, q, [this] {
            auto item = source(selectedSource); if (!item || !item->media) { message("Select a video or audio file source."); return; }
            QDialog dialog(q); dialog.setWindowTitle("Media playback"); auto *layout = new QVBoxLayout(&dialog);
            auto *pause = new QCheckBox(studioText("Paused"), &dialog); pause->setObjectName("mediaPaused"); pause->setChecked(item->mediaPaused || item->media->ended()); layout->addWidget(pause);
            auto *loop = new QCheckBox(studioText("Loop"), &dialog); loop->setObjectName("mediaLoop"); loop->setChecked(item->mediaLoop); layout->addWidget(loop);
            auto *restart = new QPushButton(studioText("Restart from beginning"), &dialog); restart->setObjectName("mediaRestart"); layout->addWidget(restart);
            QObject::connect(pause, &QCheckBox::toggled, &dialog, [this, item](bool value) { item->mediaPaused = value; if (item->media) item->media->setPaused(value); changed(); });
            QObject::connect(loop, &QCheckBox::toggled, &dialog, [this, item](bool value) { item->mediaLoop = value; if (item->media) item->media->setLoop(value); changed(); });
            QObject::connect(restart, &QPushButton::clicked, &dialog, [item, pause] { pause->setChecked(false); if (item->media) item->media->restart(); });
            auto *onTake = new QCheckBox(studioText("Restart when this source enters Program"),&dialog); onTake->setObjectName("mediaRestartOnTake"); onTake->setChecked(item->mediaRestartOnTake); layout->addWidget(onTake);
            onTake->setToolTip(studioText("Start from the beginning when the source changes from off-screen to visible in Program. Shared sources that remain visible keep playing."));
            QObject::connect(onTake,&QCheckBox::toggled,&dialog,[this,item](bool value) { item->mediaRestartOnTake = value; changed(); });
            auto *timeLabel = new QLabel(&dialog); timeLabel->setObjectName("mediaTime"); timeLabel->setWordWrap(true); layout->addWidget(timeLabel);
            auto *seek = new QSlider(Qt::Horizontal,&dialog); seek->setObjectName("mediaSeek"); seek->setAccessibleName(studioText("Playback position"));
            seek->setRange(0,int(std::min<int64_t>(INT_MAX,item->media->durationMs()))); seek->setSingleStep(1000); seek->setPageStep(5000); layout->addWidget(seek);
            seek->setToolTip(studioText("Seek within the clip. A paused video shows the picture at the new position without resuming audio."));
            QElapsedTimer cueIdle;
            QObject::connect(seek,&QSlider::sliderReleased,&dialog,[item,seek,&cueIdle] { cueIdle.restart(); if (item->media) item->media->seek(seek->value()); });
            QObject::connect(seek,&QSlider::valueChanged,&dialog,[item,seek,&cueIdle](int value) { cueIdle.restart(); if (!seek->isSliderDown() && item->media) item->media->seek(value); });
            QTimer clock; QObject::connect(&clock,&QTimer::timeout,&dialog,[item,timeLabel,seek,&cueIdle] {
                if (item->media) {
                    const bool durationKnown = item->media->durationMs() > 0;
                    seek->setEnabled(durationKnown);
                    if (durationKnown) timeLabel->setText(QString("%1 / %2 s  (%3 s remaining)").arg(item->media->positionMs()/1000.0,0,'f',1).arg(item->media->durationMs()/1000.0,0,'f',1).arg(std::max<int64_t>(0,item->media->durationMs()-item->media->positionMs())/1000.0,0,'f',1));
                    else timeLabel->setText(QString("%1 s - duration unavailable.").arg(item->media->positionMs()/1000.0,0,'f',1) +
                        (item->locator.endsWith(".mkv", Qt::CaseInsensitive) ? "\nTo restore seeking for an interrupted recording, use Recordings > Export recording as MP4..." : ""));
                    // Worker position reports trail input. Do not overwrite a
                    // keyboard repeat or rapid clicks with an older cue.
                    if (!seek->isSliderDown() && (!cueIdle.isValid() || cueIdle.elapsed()>=500)) { const QSignalBlocker block(seek); seek->setMaximum(int(std::min<int64_t>(INT_MAX,item->media->durationMs()))); seek->setValue(int(item->media->positionMs())); }
                }
            }); clock.start(200);
            QDialogButtonBox close(QDialogButtonBox::Close); layout->addWidget(&close); QObject::connect(&close, &QDialogButtonBox::rejected, &dialog, &QDialog::reject); dialog.exec();
        });
        QObject::connect(cameraSettingsButton, &QPushButton::clicked, q, [this] { openCameraSettings(); });
        sourceLayout->addLayout(editButtons);
        sourceHealth = new QLabel(studioText("Select a source to inspect its status."), q);
        sourceHealth->setObjectName("sourceHealth"); sourceHealth->setWordWrap(true); sourceLayout->addWidget(sourceHealth);
        QObject::connect(edit, &QPushButton::clicked, q, [this] { addSourceDialog(true); });
        QObject::connect(reconnect, &QPushButton::clicked, q, [this] {
            if (auto item = source(selectedSource)) {
                if (item->kind == "window") addSourceDialog(true, true);
                else { item->browserRestarts.clear(); stopSource(item); startSource(item); message("Reconnecting " + item->name); }
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
        for (const auto &name : {"Full", "Side by side", "Stacked", "Picture in picture"}) {
            auto *preset = button(name, QString("layout-")+name, layouts, q);
            if (QString(name) == "Stacked") preset->setToolTip(studioText("Stack visible, unlocked pictures vertically and Fill each tile. Lock backgrounds and overlays first to preserve their placement. Use Fit afterward to show the entire source."));
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
        layerLock = new QCheckBox(studioText("Lock"), q); layerLock->setObjectName("layerLock"); transform->addWidget(layerLock);
        snap = new QCheckBox(studioText("Snap"), q); snap->setObjectName("snapLayers"); snap->setChecked(true); transform->addWidget(snap);
        fitMode = new QComboBox(q); fitMode->setObjectName("layerFit");
        fitMode->addItem(studioText("Fit"), "fit"); fitMode->addItem(studioText("Fill"), "fill"); fitMode->addItem(studioText("Stretch"), "stretch"); transform->addWidget(fitMode);
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
        layerButtons->addWidget(new QLabel(studioText("Opacity"), q));
        opacity = new QSlider(Qt::Horizontal, q); opacity->setObjectName("layerOpacity"); opacity->setRange(0, 100); opacity->setValue(100); layerButtons->addWidget(opacity);
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

        audioBox = new QGroupBox(studioText("Audio mixer"), panels); auto *audioLayout = new QVBoxLayout(audioBox);
        auto *scroll = new QScrollArea(audioBox); scroll->setObjectName("audioTracksScroll"); scroll->setWidgetResizable(true);
        scroll->setStyleSheet("QScrollArea { border:1px solid #50667b; background:#101b27; }");
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        scroll->setMinimumHeight(170);
        auto *audioWidget = new QWidget(scroll); audioWidget->setObjectName("studioAudioTracks");
        audioWidget->setStyleSheet("QWidget#studioAudioTracks { background:#080f17; }");
        audioRows = new QVBoxLayout(audioWidget); scroll->setWidget(audioWidget);
        audioLayout->addWidget(scroll);
        auto *master = new QHBoxLayout; master->addWidget(new QLabel(studioText("Master"), q));
        auto *masterSlider = new QSlider(Qt::Horizontal, q); masterSlider->setRange(0, 150); masterSlider->setValue(100);
        masterSlider->setObjectName("masterGain"); master->addWidget(masterSlider);
        audioLayout->addLayout(master);
        masterMeter = new AudioLevelMeter(audioBox); masterMeter->setObjectName("masterAudioMeter");
        masterMeter->setAccessibleName(studioText("Master audio level (dBFS)")); audioLayout->addWidget(masterMeter);
        QObject::connect(masterSlider, &QSlider::valueChanged, q, [this](int value) { masterGain = value/100.0f; changed(); });
        monitorEnable = new QCheckBox(studioText("Headphone monitor"), q); monitorEnable->setObjectName("monitorEnable");
        monitorDevice = new QComboBox(q); monitorDevice->setObjectName("monitorDevice");
        monitorDevice->setAccessibleName(studioText("Monitoring output device"));
        monitorDevice->setToolTip(studioText("Choose the headphones or speakers used for Listen. This does not change recording or streaming audio."));
        audioLayout->addWidget(monitorEnable);
        auto *monitorRow = new QHBoxLayout; monitorRow->addWidget(monitorDevice, 1);
        auto *refreshMonitor = button("Refresh", "refreshMonitorDevices", monitorRow, q);
        refreshMonitor->setToolTip(studioText("Refresh playback devices after connecting headphones or an audio interface."));
        audioLayout->addLayout(monitorRow);
        refreshMonitorDevices();
        auto *monitorVolume = new QSlider(Qt::Horizontal, q); monitorVolume->setRange(0, 100); monitorVolume->setValue(50);
        monitorVolume->setAccessibleName(studioText("Headphone volume")); monitorVolume->setObjectName("monitorVolume"); audioLayout->addWidget(monitorVolume);
        QObject::connect(monitorEnable, &QCheckBox::toggled, q, [this](bool value) { monitoring = value; });
        QObject::connect(monitorDevice, &QComboBox::currentIndexChanged, q, [this] {
            monitorDeviceKey.store(std::make_shared<const QString>(monitorDevice->currentData(Qt::UserRole+1).toString()));
            if (qApp->property("studioShow").toString().isEmpty()) {
                QSettings settings("VDO.Ninja", "Video Mixer");
                settings.setValue("mixer/monitorDevice", monitorDevice->currentData(Qt::UserRole+1));
                settings.setValue("mixer/monitorDeviceName", monitorDevice->currentData(Qt::UserRole+2));
            }
        });
        QObject::connect(refreshMonitor, &QPushButton::clicked, q, [this] { refreshMonitorDevices(); });
        QObject::connect(monitorVolume, &QSlider::valueChanged, q, [this](int value) { monitorGain = value/100.0f; });
        auto *monitorHelp = new QLabel(studioText("Scene follows Program. Listen also hears sources off air. Use headphones for monitoring and guest talkback. Mics start with Listen off."), q);
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
        status = new QLabel(studioText("Add cameras, windows, audio, or browser sources to get started."), q);
        status->setObjectName("mixerStatus"); status->setWordWrap(true); status->setTextInteractionFlags(Qt::TextSelectableByMouse);
        status->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Preferred);
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
            gain->setObjectName("audio_gain_"+item->id);
            gain->setAccessibleName(item->name+" volume"); controls->addWidget(gain);
            auto *mute = new QCheckBox(studioText("Mute"), row); mute->setChecked(item->muted); controls->addWidget(mute);
            auto *follow = new QCheckBox(studioText("Scene"), row); follow->setChecked(item->followsScene); controls->addWidget(follow);
            auto *listen = new QCheckBox(studioText("Listen"), row); listen->setChecked(item->monitored); controls->addWidget(listen);
            auto *onAir = new QCheckBox(studioText("On air"), row); onAir->setChecked(item->programAudio); controls->addWidget(onAir);
            onAir->setObjectName("audio_program_"+item->id); onAir->setAccessibleName(item->name+" - On air");
            onAir->setToolTip(studioText("Allow this track into recording and streaming, subject to Mute and Scene. Turning this off keeps admitted guest conversation and headphone Listen available."));
            QObject::connect(onAir, &QCheckBox::toggled, q, [this,item](bool value) { item->programAudio = value; changed(); });
            layout->addLayout(controls);
            auto *channels = new QComboBox(row); channels->addItems({"Stereo", "Mono (L + R)", "Left to both", "Right to both"}); channels->setCurrentIndex(item->channelMode);
            channels->setObjectName("audio_channels_"+item->id); channels->setAccessibleName(item->name+" audio channels");
            channels->setToolTip(studioText("Choose Left or Right to both for an interface microphone heard in only one ear. Mono averages both channels. Applies to Program, guest returns and Listen.")); layout->addWidget(channels);
            QObject::connect(channels, &QComboBox::currentIndexChanged, q, [this,item](int mode) { item->channelMode = mode; changed(); });
            auto *effects = new QHBoxLayout;
            auto *noise = new QCheckBox(studioText("Noise suppression"), row); noise->setChecked(item->noiseSuppression); effects->addWidget(noise);
            auto *gate = new QCheckBox(studioText("Gate"), row); gate->setChecked(item->noiseGate); effects->addWidget(gate);
            auto *compress = new QCheckBox(studioText("Compress"), row); compress->setChecked(item->compression); effects->addWidget(compress);
            for (auto *check : {mute, follow, listen, noise, gate, compress}) {
                check->setAccessibleName(item->name+" - "+check->text());
                check->setObjectName(QString("audio_%1_%2").arg(check == mute ? "mute" : check == follow ? "follow" : check == listen ? "listen" : check == noise ? "noise" : check == gate ? "gate" : "compress", item->id));
            }
            follow->setToolTip(studioText("Follow the source's Program scene, including fades. Leave off for podcast voices that must stay audible when their video is off-screen. Mute and Backstage still silence them."));
            listen->setToolTip(studioText("Include this track in headphone monitoring when monitoring is enabled."));
            noise->setToolTip(studioText("RNNoise speech noise suppression. Leave off for music and game audio."));
            gate->setToolTip(studioText("Soft noise gate at -45 dBFS.")); compress->setToolTip(studioText("4:1 compression above -18 dBFS, without automatic makeup gain."));
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
        modeRow->addWidget(new QLabel(studioText("Capture mode"), &dialog));
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
        auto *feedback = new QLabel(studioText("Adjust a setting to see the result in the live preview."), &dialog);
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
                auto *automatic = new QCheckBox(studioText("Auto"), panel); automatic->setObjectName("cameraAuto_"+id);
                automatic->setChecked(control.automatic); automatic->setEnabled(control.supportsAuto && control.supportsManual); row->addWidget(automatic);
                auto *reset = button("Reset", "cameraReset_"+id, row, panel);
                reset->setToolTip(studioText("Use the driver's default value, with automatic mode when supported."));
                const auto range = QString("Range %1 to %2; step %3. Values are camera driver units.").arg(control.minimum).arg(control.maximum).arg(control.step);
                number->setToolTip(range); slider->setToolTip(range); label->setToolTip(range);
                if (id == "exposure") label->setToolTip(studioText("Exposure uses powers of two seconds: -7 is 1/128 second. Longer exposure can reduce frame rate."));
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
            if (settings.controls.empty()) rows->addWidget(new QLabel(studioText("This camera's driver exposes no adjustable image controls."), panel));
            rows->addStretch(); scroll->setWidget(panel);
        };
        bool wasStarting = false;
        QObject::connect(applyMode, &QPushButton::clicked, &dialog, [&] {
            const auto index = mode->currentIndex(); if (index < 0 || static_cast<size_t>(index) >= formats.size()) return;
            const auto requested = formats[index];
            item->cameraWidth = requested.width; item->cameraHeight = requested.height; item->cameraFps = requested.fps;
            stopSource(item); startSource(item);
            wasStarting = true; feedback->setText(studioText("Applying capture mode...")); applyMode->setEnabled(false); changed();
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
        QLabel help; help.setObjectName("sourceValidation"); help.setWordWrap(true);
        auto validationError = [&](const QString &text) { help.setText(text); announce(&help, text); };
        QCheckBox transparent("Transparent overlay background (up to 15 fps)"); transparent.setObjectName("browserTransparency");
        QCheckBox colorMetadata("Preserve original video color metadata"); colorMetadata.setObjectName("browserColorMetadata");
        colorMetadata.setToolTip(studioText("Enable if a WebRTC source's colors look wrong. On affected WebView2 versions this may use software decoding and more CPU."));
        QSpinBox audioDelay; audioDelay.setObjectName("sourceAudioDelay"); audioDelay.setRange(0, 500); audioDelay.setSuffix(" ms");
        QSpinBox browserWidth, browserHeight;
        browserWidth.setObjectName("browserWidth"); browserWidth.setRange(160,3840); browserWidth.setValue(existing ? existing->browserWidth : Width);
        browserHeight.setObjectName("browserHeight"); browserHeight.setRange(120,3840); browserHeight.setValue(existing ? existing->browserHeight : Height);
        browserWidth.setAccessibleName("Browser viewport width"); browserHeight.setAccessibleName("Browser viewport height");
        audioDelay.setToolTip(studioText("Delay this source's Program audio to match its picture. Headphone monitoring stays immediate."));
        form->addRow("Source type", &type); form->addRow("Name", &name); form->addRow("Device / window", &device);
        form->addRow("URL / file / text / color", &value); form->addRow(&browse); form->addRow(&transparent); form->addRow(&colorMetadata); form->addRow(&help);
        form->addRow("Audio delay", &audioDelay);
        form->addRow("Browser width", &browserWidth); form->addRow("Browser height", &browserHeight);
        if (editing) form->addRow(new QLabel(studioText("Source changes apply to every scene, including Program."), &dialog));
        auto populate = [&] {
            const auto kind = type.currentData().toString(); device.clear();
            transparent.setVisible(kind == "browser" || kind == "guest" || kind == "wireless" || kind == "chat");
            colorMetadata.setVisible(!transparent.isHidden());
            form->setRowVisible(&browserWidth, !transparent.isHidden()); form->setRowVisible(&browserHeight, !transparent.isHidden());
            if (!existing) transparent.setChecked(kind == "browser" || kind == "chat");
            audioDelay.setEnabled(kind == "browser" || kind == "guest" || kind == "wireless" || kind == "window" || kind == "microphone" || kind == "desktop" || kind == "media");
            audioDelay.setValue(kind == "browser" || kind == "guest" || kind == "wireless" ? 60 : 0);
            value.clear(); value.setVisible(true); device.setVisible(true); browse.setVisible(kind == "image" || kind == "color" || kind == "media");
            if (kind == "window" || kind == "camera" || kind == "spout") {
                const auto devices = kind == "camera" ? core->listCameras() : (kind == "spout" ? core->listSpoutSenders() : core->listWindows());
                for (const auto &info : devices) device.addItem(QString::fromStdString(info.name), QString::fromStdString(info.id));
                value.setVisible(false);
                help.setText(studioText("Select a source. Window audio is captured when supported by Windows."));
            } else if (kind == "microphone") {
                device.addItem(studioText("Default microphone"), "");
                for (const auto &info : core->listAudioInputDevices()) device.addItem(QString::fromStdString(info.name), QString::fromStdString(info.id));
                value.setVisible(false); help.setText(studioText("Microphones stay on across scenes by default. Use Mute or enable Scene in the audio mixer."));
            } else {
                device.setVisible(false);
                if (kind == "desktop") { value.setVisible(false); help.setText(studioText("Captures other applications, excluding Desktop Studio, its browsers and monitoring. Avoid adding the same game's individual audio twice.")); }
                if (!transparent.isHidden()) { value.setPlaceholderText("https://..."); help.setText(studioText("Overlay mode clears the page background automatically, keeping message and card styling. Turn it off for smooth browser video at 30 fps. Open / Invite allows interaction; Escape parks the browser.")); }
                if (kind == "media") help.setText(studioText("Local video or audio file. Playback uses Windows media decoders; H.264/AAC MP4 is recommended. Use Playback controls after adding it."));
                if (kind == "image") help.setText(studioText("PNG, JPEG and other Qt-supported images. Transparent PNGs remain transparent."));
                if (kind == "title") { value.setText(studioText("Your title")); help.setText(studioText("A full-frame title card. Resize and position it on the canvas.")); }
                if (kind == "color") { value.setText(studioText("#24466f")); help.setText(studioText("A solid color layer.")); }
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
            if (device.isVisible() && device.count() == 0) { validationError(studioText("No source found. Connect a device or open a window first.")); return; }
            if (transparent.isVisible() && !webUrl(value.text().trimmed())) { validationError(studioText("Enter a complete http:// or https:// source URL.")); return; }
            if (kind == "color" && !QColor(value.text()).isValid()) { validationError(studioText("Enter a valid color, for example #24466f.")); return; }
            if (kind == "media" && !QFileInfo(value.text()).isFile()) { validationError(studioText("Choose an existing local media file.")); return; }
            if (kind == "image" && !QImageReader(value.text()).canRead()) { validationError(studioText("Choose a readable image file.")); return; }
            auto item = existing ? existing : std::make_shared<Source>();
            const auto locator = device.isVisible() ? device.currentData().toString() : value.text().trimmed();
            // Labels and audio delay do not change the capture session. Keeping it
            // alive avoids dropping video/audio or rejoining a remote guest.
            const bool restart = existing && (reconnect || kind != item->kind ||
                locator != item->locator || transparent.isChecked() != item->transparent || colorMetadata.isChecked() != item->preserveColorMetadata ||
                browserWidth.value() != item->browserWidth || browserHeight.value() != item->browserHeight);
            if (restart && device.isVisible() && device.currentData(Qt::UserRole+1).toBool()) {
                validationError(studioText("The saved device is unavailable. Select an available device to reconnect.")); return;
            }
            if (restart) { item->browserRestarts.clear(); stopSource(item); }
            item->kind = kind; item->transparent = transparent.isChecked();
            item->preserveColorMetadata = colorMetadata.isChecked();
            item->browserWidth = browserWidth.value(); item->browserHeight = browserHeight.value();
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
            colorMetadata.setChecked(existing->preserveColorMetadata);
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
            item->media->onDiscontinuity([weak = std::weak_ptr<Source>(item)] {
                if (auto source = weak.lock()) { std::lock_guard lock(source->audioMutex); source->samples.clear(); source->delayedSamples.clear(); source->appliedDelayMs = -1; }
            });
            item->media->setImageCallback(imageFrame);
            item->media->setPaused(item->mediaPaused); item->media->start(item->locator, frame, audioChunk);
        } else if (item->kind == "browser" || item->kind == "guest" || item->kind == "wireless" || item->kind == "chat") {
            if (!webUrl(item->locator)) { item->error = "Invalid browser URL."; return; }
            item->browser = new BrowserSource(item->id, QUrl(item->locator), true, item->transparent, false, item->preserveColorMetadata, QSize(item->browserWidth, item->browserHeight));
            QObject::connect(item->browser, &BrowserSource::restartRequired, q, [this, weak, valid] {
                if (auto item = weak.lock(); item && *valid && running) {
                    const auto now = now100ns();
                    while (!item->browserRestarts.empty() && now-item->browserRestarts.front() > 600000000)
                        item->browserRestarts.pop_front();
                    stopSource(item);
                    if (item->browserRestarts.size() >= 3) {
                        item->error = "Browser repeatedly crashed or stalled. Automatic reconnect paused; use Reconnect to try again.";
                        message(item->name+": "+item->error);
                    } else { item->browserRestarts.push_back(now); startSource(item); }
                }
            }, Qt::QueuedConnection);
            QObject::connect(item->browser, &BrowserSource::recovered, q, [weak, valid] { if (auto item = weak.lock(); item && *valid) item->error.clear(); });
            QObject::connect(item->browser, &BrowserSource::frameReady, q, [this, weak, valid](const QImage &image) {
                if (auto item = weak.lock(); item && *valid) {
                    { std::lock_guard lock(item->imageMutex); item->image = image; item->lastFrame = now100ns(); ++item->frames; }
                    // A replacement proxy starts with no error, so it need not
                    // emit recovered(). Retire its previous warning when usable
                    // pictures return, without overwriting a later user message.
                    if (item->error.isEmpty() && !item->browserStatusMessage.isEmpty()) {
                        if (status->text() == item->browserStatusMessage) message(item->name+": Browser video resumed.");
                        item->browserStatusMessage.clear();
                    }
                }
            });
            QObject::connect(item->browser, &BrowserSource::errorOccurred, q, [this, weak, valid](const QString &message) {
                if (auto item = weak.lock(); item && *valid) {
                    item->error = message;
                    { std::lock_guard lock(item->imageMutex); item->image = {}; }
                    item->browserStatusMessage = item->name+": "+message;
                    this->message(item->browserStatusMessage);
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
        recordAudioSources.removeAll(item->id); recordIsoSources.removeAll(item->id);
        // Destroy capture owners on the UI thread, including WebView2's apartment.
        item->onProgram = false;
        if (auto output = item->guestReturn.exchange({})) QThreadPool::globalInstance()->start([output] { output->stop(); });
        stopSource(item);
        selectedSource.clear(); rebuild(); changed();
    }

    void populateMonitorDevices(const std::vector<MonitorDevice> &devices) {
        const QSettings settings("VDO.Ninja", "Video Mixer");
        const auto key = monitorDevice->count() ? monitorDevice->currentData(Qt::UserRole+1).toString() : settings.value("mixer/monitorDevice", "default").toString();
        const auto name = monitorDevice->count() ? monitorDevice->currentData(Qt::UserRole+2).toString() : settings.value("mixer/monitorDeviceName", key).toString();
        const QSignalBlocker block(monitorDevice);
        monitorDevice->clear();
        for (const auto &device : devices) {
            monitorDevice->addItem(device.name, device.id);
            monitorDevice->setItemData(monitorDevice->count()-1, device.key, Qt::UserRole+1);
            monitorDevice->setItemData(monitorDevice->count()-1, device.name, Qt::UserRole+2);
            monitorDevice->setItemData(monitorDevice->count()-1, device.name, Qt::ToolTipRole);
        }
        int selected = monitorDevice->findData(key, Qt::UserRole+1);
        if (selected < 0) {
            monitorDevice->addItem(studioText("Unavailable: ")+name, -2); selected = monitorDevice->count()-1;
            monitorDevice->setItemData(selected, key, Qt::UserRole+1);
            monitorDevice->setItemData(selected, name, Qt::UserRole+2);
            monitorEnable->setChecked(false);
        }
        monitorDevice->setCurrentIndex(selected);
        monitorDeviceKey.store(std::make_shared<const QString>(key));
    }
    void refreshMonitorDevices() {
        if (monitorDevicesRefreshing) return;
        if (!monitorDevice->count()) populateMonitorDevices({{-1, "Default Windows playback device", "default"}});
        monitorDevicesRefreshing = true;
        auto promise = std::make_shared<std::promise<std::vector<MonitorDevice>>>();
        auto future = std::make_shared<std::future<std::vector<MonitorDevice>>>(promise->get_future());
        QThreadPool::globalInstance()->start([promise] { promise->set_value(AudioMonitor::devices()); });
        auto *poll = new QTimer(q); poll->setInterval(50);
        QObject::connect(poll, &QTimer::timeout, q, [this, future, poll] {
            if (future->wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
            poll->stop(); poll->deleteLater(); monitorDevicesRefreshing = false;
            populateMonitorDevices(future->get());
        });
        poll->start();
    }
    void updateInviteReturn(const std::shared_ptr<Source> &item) {
        QUrl url(item->invite); QUrlQuery query(url);
        query.removeAllQueryItems("view"); query.removeAllQueryItems("novideo"); query.removeAllQueryItems("noaudio");
        // An explicitly empty view list prevents a send-only room member from
        // receiving all room peers. novideo controls incoming video, not capture.
        query.addQueryItem("view", item->returnMode == "none" ? QString() : item->returnId);
        if (item->returnMode != "video") query.addQueryItem("novideo", "");
        if (item->returnMode == "none") query.addQueryItem("noaudio", "");
        url.setQuery(query); item->invite = url.toString(QUrl::FullyEncoded);
    }
    void refreshGuestReturns() {
        for (const auto &item : sources) {
            const bool enable = guestReturnEnabled->isChecked() && !item->returnId.isEmpty() && item->returnMode != "none";
            if (enable && !item->guestReturn.load()) item->guestReturn.store(std::make_shared<GuestReturn>(item->returnId, roomPassword, room, Width, Height));
            if (!enable) if (auto previous = item->guestReturn.exchange({})) QThreadPool::globalInstance()->start([previous] { previous->stop(); });
        }
    }
    std::shared_ptr<Source> createInvite(bool cameraOnly, const QString &name) {
        if (sources.size() >= 32) { message("This version supports up to 32 sources."); return {}; }
        auto item = std::make_shared<Source>(); item->kind = cameraOnly ? "wireless" : "guest"; item->name = name.trimmed();
        item->followsScene = cameraOnly;
        item->returnMode = cameraOnly ? "none" : "video";
        item->audioDelayMs = 60; item->admitted = false; item->returnId = "return"+newId().remove('-').left(24);
        const auto stream = "mix" + newId().remove('-'); const auto password = roomPassword;
        QUrl view("https://vdo.ninja/"); QUrlQuery viewQuery;
        viewQuery.addQueryItem("view", stream); viewQuery.addQueryItem("password", password);
        viewQuery.addQueryItem("room", room);
        viewQuery.addQueryItem("solo", ""); viewQuery.addQueryItem("cleanoutput", ""); view.setQuery(viewQuery); item->locator = view.toString(QUrl::FullyEncoded);
        QUrl push("https://vdo.ninja/"); QUrlQuery pushQuery;
        pushQuery.addQueryItem("push", stream); pushQuery.addQueryItem("password", password); pushQuery.addQueryItem("label", item->name);
        pushQuery.addQueryItem("room", room); pushQuery.addQueryItem("view", item->returnId);
        if (cameraOnly) pushQuery.addQueryItem("rear", "");
        push.setQuery(pushQuery); item->invite = push.toString(QUrl::FullyEncoded);
        updateInviteReturn(item); return item;
    }
    void addInvite(bool cameraOnly) {
        bool ok; const auto name = QInputDialog::getText(q, cameraOnly ? "Wireless camera" : "Invite guest", "Source name", QLineEdit::Normal,
            cameraOnly ? "Wireless camera" : "Guest", &ok);
        if (!ok || name.trimmed().isEmpty()) return;
        auto item = createInvite(cameraOnly, name); if (!item) return; appendSource(item);
        if (!cameraOnly) guestReturnEnabled->setChecked(true);
        refreshGuestReturns(); showInvite(item);
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
        auto *help = new QLabel(studioText("Send this private link to the guest or open it on your phone.\nThey choose their camera and microphone, then join. Use headphones.\nPrivate talkback sends your microphone inputs only to guests.\nGuests join backstage. Select their source and choose Admit / Backstage when ready. Program return sends the show with their own audio removed."), &dialog); layout->addWidget(help);
        auto *url = new QLineEdit(item->invite, &dialog); url->setReadOnly(true); url->setMinimumWidth(620); url->setObjectName("inviteUrl"); layout->addWidget(url);
        auto *returnChoice = new QComboBox(&dialog); returnChoice->setObjectName("inviteReturnMode");
        returnChoice->setAccessibleName(studioText("Return to invited source"));
        returnChoice->addItem(studioText("No return (send-only camera)"), "none");
        returnChoice->addItem(studioText("Audio return only (mix-minus)"), "audio");
        returnChoice->addItem(studioText("Program video and mix-minus audio"), "video");
        returnChoice->setCurrentIndex(returnChoice->findData(item->returnMode)); layout->addWidget(returnChoice);
        auto *returnHelp = new QLabel(studioText("Admitted guests stay audible on Program and hear one another even off-screen, without their own voice. Use Mute or Backstage to silence them, or enable Scene to follow their picture.\nChanging return mode updates the invite link; reopen the new link on the device. Wireless cameras default to no return."), &dialog);
        returnHelp->setWordWrap(true); layout->addWidget(returnHelp);
        QObject::connect(returnChoice, &QComboBox::currentIndexChanged, &dialog, [this, item, returnChoice, url] {
            item->returnMode = returnChoice->currentData().toString(); updateInviteReturn(item); url->setText(item->invite);
            if (item->returnMode != "none") guestReturnEnabled->setChecked(true);
            refreshGuestReturns(); changed();
        });
        auto *buttons = new QHBoxLayout;
        auto *copy = button("Copy invite link", "copyInvite", buttons, &dialog);
        auto *open = button("Open invite", "openInvite", buttons, &dialog);
        auto *inspect = button("Interact with receiver", "inspectReceiver", buttons, &dialog);
        auto *close = button("Done", "closeInvite", buttons, &dialog); layout->addLayout(buttons);
        QObject::connect(copy, &QPushButton::clicked, &dialog, [url, copy] { QApplication::clipboard()->setText(url->text()); copy->setText(studioText("Copied")); });
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
        } else if (name == "Stacked") {
            for (size_t i = 0; i < layers.size(); ++i) {
                layers[i]->rect = {0, static_cast<double>(i)/layers.size(), 1, 1.0/layers.size()};
                layers[i]->fit = "fill";
            }
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
        updateAudioRouting(fade); changed(); message("Program: " + program.name);
    }
    void updateAudioRouting(bool fade = false) {
        const auto tick = now100ns();
        for (auto &item : sources) {
            const bool active = std::any_of(program.layers.begin(), program.layers.end(),
                [&](const auto &layer) { return layer.source == item->id && layer.visible && item->admitted; });
            if (active && !item->onProgram && item->mediaRestartOnTake && item->media) {
                item->mediaPaused = false; item->media->restart();
                std::lock_guard lock(item->audioMutex); item->samples.clear(); item->delayedSamples.clear(); item->appliedDelayMs = -1;
            }
            item->onProgram = active;
            const auto old = item->audioRoute.load();
            item->audioRoute.store(std::make_shared<const SceneAudioRoute>(SceneAudioRoute{
                fade ? old->at(tick) : float(active), float(active), fade ? tick : 0}));
        }
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
            if (slot == 1) { programRenderer = "passthrough"; compositorError.clear(); }
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
            if (!image.isNull()) {
                if (slot == 1) { programRenderer = "gpu"; compositorError.clear(); }
                cached = {scene.background, std::move(keys), std::move(image)}; return cached.image;
            }
            if (!gpu[slot].error().isEmpty()) message(gpu[slot].error());
        }
        QImage image(Width, Height, scene.background.alpha() == 255 ? QImage::Format_RGB32 : QImage::Format_ARGB32_Premultiplied);
        if (slot == 1) { programRenderer = "cpu"; compositorError = renderer->currentIndex() == 0 ? gpu[slot].error() : QString(); }
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
    QImage composeProgram() {
        if (privateBreak) {
            if (breakSlate.size() != QSize(Width, Height)) {
                breakSlate = QImage(Width, Height, QImage::Format_RGB32); breakSlate.fill(QColor("#101722"));
                QPainter painter(&breakSlate); painter.setPen(Qt::white); QFont font; font.setPixelSize(std::min(Width,Height)/12); painter.setFont(font);
                painter.drawText(breakSlate.rect(), Qt::AlignCenter, studioText("Be right back"));
            }
            return breakSlate;
        }
        QImage frame = compose(program, 1);
        if (fading) {
            const double fraction = std::min(1.0, transition.elapsed()/400.0);
            QImage old = compose(previous, 2); QPainter painter(&old); painter.setOpacity(fraction); painter.drawImage(0, 0, frame); painter.end(); frame = old;
            if (fraction >= 1) fading = false;
        }
        return frame;
    }
    void render() {
        const auto talkText = studioText(privateTalkback || heldTalkback ? "Talking privately" : "Private talkback");
        if (talkbackButton->text() != talkText) { talkbackButton->setText(talkText); talkbackButton->setAccessibleName(talkText); }
        const int64_t tick = now100ns();
        const int64_t period = 10000000/canvasFps;
        if (!nextRender || tick-nextRender > period*2) nextRender = tick;
        nextRender += period;
        videoTimer.setInterval(std::max(1, int((nextRender-tick+9999)/10000)));
        recordOutput->setText(recordingActive() ? studioText("Stop recording") : studioText("Record..."));
        recordOutput->setAccessibleName(recordOutput->text());
        const auto remaining = recorder.remainingSeconds();
        auto recordingState = recorder.status();
        const auto plan = recordingPlan.load();
        for (const auto &iso : plan->isos) recordingState += " | "+iso.recorder->status();
        const auto recordingText = recordingState + (recorder.active() && remaining >= 0 ? QString(" / ~%1 min disk remaining").arg(remaining/(60*(1+2*plan->isos.size()))) : QString());
        if (recordingStatus->text() != recordingText) {
            recordingStatus->setText(recordingText); recordingStatus->setVisible(!recordingText.isEmpty());
            // Disk estimates remain readable without repeatedly interrupting
            // a screen reader during an otherwise unchanged recording.
            if (announcedRecordingStatus != recordingState) {
                announcedRecordingStatus = recordingState; announce(recordingStatus, recordingText);
            }
        }
        canvasFormat->setEnabled(!core->isLive() && !recordingBusy() && !guestReturnEnabled->isChecked() && !virtualCamera->isChecked() && !spoutOutput->isChecked());
        recordOutput->setEnabled(!recordingBusy() || recordingActive());
        if (studioMode && previewView->isVisible()) {
            const auto item = source(selectedSource);
            const auto selection = item && item->hasVideo() && selectedLayer() ? selectedLayer()->rect : QRectF();
            previewView->present(compose(preview(), 0), selection);
        }
        const QImage frame = composeProgram();
        if (studioMode) programView->present(frame);
        else if (previewView->isVisible()) {
            const auto item = source(selectedSource);
            const auto layer = selectedLayer();
            const auto selection = item && item->hasVideo() && layer && layer->visible && layer->opacity > 0 ? layer->rect : QRectF();
            previewView->present(frame, selection);
        }
        if (outputView) outputView->present(frame);
        recorder.submit(frame, tick);
        for (const auto &iso : recordingPlan.load()->isos) if (iso.recorder->active()) {
            QImage picture; auto item = source(iso.id);
            if (item && item->admitted && !privateBreak) { std::lock_guard lock(item->imageMutex); picture = item->image; }
            if (picture.isNull()) { picture = QImage(iso.size,QImage::Format_RGB32); picture.fill(Qt::black); }
            else if (picture.size() != iso.size) picture = picture.scaled(iso.size,Qt::IgnoreAspectRatio,Qt::SmoothTransformation);
            iso.recorder->submit(picture,tick);
        }
        localOutputs.submit(frame, tick);
        for (const auto &item : sources) if (item->returnMode == "video") if (auto output = item->guestReturn.load()) output->video(frame, tick);
        const bool publishingProgram = core->isLive() && core->getSourceHealth().mode == app::VideoSourceMode::Mixer;
        previewTitle->setText(QString(studioMode ? "PREVIEW  /  " : publishingProgram ? "LIVE OUTPUT  /  " : "OUTPUT  /  ") + preview().name);
        programTitle->setText(privateBreak ? "PRIVATE BREAK / Program silent" : QString(publishingProgram ? "LIVE OUTPUT  /  " : "PROGRAM  /  ") + program.name);
        if (privateBreak && !studioMode) previewTitle->setText("PRIVATE BREAK / Program silent");
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
            } else sourceHealth->setText(studioText("Select a source to inspect its status."));
        }
    }
    void mixAudio() {
        auto next = std::chrono::steady_clock::now();
        IsolatedAudioMonitor monitor;
        std::shared_ptr<const QString> openedDevice;
        while (running) {
            const auto audioTime = next;
            next += std::chrono::milliseconds(10);
            audio::StreamChunk chunk; chunk.samples.assign(960, 0);
            const auto recording = recordingPlan.load();
            const bool separateAudio = recordingActive() && (!recording->audioIds.empty() || !recording->isos.empty());
            std::vector<std::vector<float>> audioTracks,isoAudio;
            if (separateAudio) { audioTracks.assign(recording->audioIds.size(),std::vector<float>(960,0)); isoAudio.assign(recording->isos.size(),std::vector<float>(960,0)); }
            // Each chunk contains exactly 10 ms of samples. After a scheduling
            // stall we can produce several catch-up chunks in one millisecond;
            // wall-clock stamping would overlap them and corrupt AAC timing.
            chunk.captureTime100ns = std::chrono::duration_cast<std::chrono::nanoseconds>(audioTime.time_since_epoch()).count()/100;
            std::vector<float> headphones(960, 0), hostTalk(960, 0), conversationExtra(960, 0);
            struct ReturnMix { std::shared_ptr<GuestReturn> output; std::vector<float> own; };
            std::vector<ReturnMix> returns;
            {
            std::lock_guard sourcesLock(sourcesMutex);
            for (const auto &item : sources) {
                if (!item->audioActive) continue;
                float peak = 0; const bool microphone = item->kind == "microphone";
                const float sourceGain = item->muted || (microphonesMuted && microphone) ? 0 : item->gain.load();
                const bool talking = (privateTalkback || heldTalkback || privateBreak || !item->programAudio) && microphone;
                const auto route = item->audioRoute.load();
                const float routeStart = route->at(chunk.captureTime100ns);
                const float routeStep = (route->at(chunk.captureTime100ns+100000)-routeStart)/480.f;
                const float audibleGain = !talking && item->admitted ? sourceGain : 0;
                const bool followsScene = item->followsScene.load();
                const bool programAllowed = item->programAudio;
                // Admitted callers keep talking when their picture is off air.
                // Program follows each track's Scene option; backstage stays private.
                const bool conversationGuest = item->kind == "guest" && item->admitted;
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
                const int channelMode = item->channelMode;
                if (channelMode) for (size_t n=0; n<processed.size(); n+=2) {
                    const float mono = channelMode == 2 ? processed[n] : channelMode == 3 ? processed[n+1] : (processed[n]+processed[n+1])*.5f;
                    processed[n] = processed[n+1] = mono;
                }
                item->processor.process(processed, item->noiseSuppression, item->noiseGate, item->compression);
                std::vector<float> isolated;
                if (separateAudio) isolated.resize(960);
                for (size_t i = 0; i < chunk.samples.size(); ++i) {
                    const float gain = programAllowed ? audibleGain*(followsScene ? routeStart+routeStep*float(i/2) : 1.f) : 0;
                    const float conversationGain = conversationGuest ? sourceGain : gain;
                    const float sample = processed[i];
                    float delayed = sample;
                    if (delay) {
                        item->delayedSamples.push_back(sample);
                        delayed = item->delayedSamples.front(); item->delayedSamples.pop_front();
                    }
                    if (separateAudio) isolated[i] = !privateBreak && item->admitted && programAllowed && !talking ? delayed*sourceGain : 0.f;
                    chunk.samples[i] += delayed*gain;
                    conversationExtra[i] += delayed*(conversationGain-gain);
                    if (output) returns.back().own[i] = delayed*conversationGain;
                    if (talking) hostTalk[i] += delayed*sourceGain;
                    peak = std::max(peak, std::abs(sample*sourceGain));
                    if (listen) headphones[i] += sample*sourceGain;
                }
                item->peak = std::max(peak, item->peak.load()*0.92f);
                if (separateAudio) {
                    float isolatedPeak = 0; for (float sample : isolated) isolatedPeak = std::max(isolatedPeak,std::abs(sample));
                    if (isolatedPeak > .98f) for (float &sample : isolated) sample *= .98f/isolatedPeak;
                    const auto index = recording->audioIds.indexOf(item->id); if (index >= 0) audioTracks[index] = isolated;
                    for (size_t n=0; n<recording->isos.size(); ++n) if (recording->isos[n].id == item->id) isoAudio[n] = isolated;
                }
            }
            }
            for (auto &bus : returns) {
                float peak = 0;
                for (size_t i = 0; i < bus.own.size(); ++i) { bus.own[i] = (chunk.samples[i]+conversationExtra[i]-bus.own[i])*masterGain+hostTalk[i]; peak = std::max(peak, std::abs(bus.own[i])); }
                if (peak > .98f) for (auto &sample : bus.own) sample *= .98f/peak;
                bus.output->audio(std::move(bus.own), chunk.captureTime100ns);
            }
            float peak = 0;
            if (privateBreak) std::fill(chunk.samples.begin(), chunk.samples.end(), 0.f);
            for (auto &value : chunk.samples) { value *= masterGain; peak = std::max(peak, std::abs(value)); }
            if (peak > 0.98f) for (auto &value : chunk.samples) value *= 0.98f/peak;
            masterPeak = std::min(0.98f, peak);
            if (monitoring) {
                const auto device = monitorDeviceKey.load();
                if (openedDevice != device) {
                    monitor.open(*device); openedDevice = device;
                }
                const auto error = monitor.error();
                if (!error.isEmpty()) {
                        monitoring = false;
                        QMetaObject::invokeMethod(q, [this, error] { monitorEnable->setChecked(false); message(error); }, Qt::QueuedConnection);
                }
                const float level = masterGain.load()*monitorGain.load();
                for (auto &sample : headphones) sample = std::clamp(sample*level, -0.98f, 0.98f);
                if (monitoring) monitor.write(headphones);
            } else if (openedDevice) { monitor.close(); openedDevice.reset(); }
            recorder.audio(chunk.samples, chunk.captureTime100ns);
            for (size_t n=0; n<audioTracks.size(); ++n) recorder.audio(audioTracks[n],chunk.captureTime100ns,int(n)+1);
            for (size_t n=0; n<isoAudio.size(); ++n) recording->isos[n].recorder->audio(isoAudio[n],chunk.captureTime100ns);
            if (core->isLive()) core->submitMixerAudio(std::move(chunk));
            if (next < std::chrono::steady_clock::now()-std::chrono::milliseconds(100)) next = std::chrono::steady_clock::now();
            std::this_thread::sleep_until(next);
        }
    }

    QJsonObject showDocument(const QString &path, bool absoluteAssets = false) {
        QJsonArray jsonSources, jsonScenes;
        for (const auto &item : sources) {
            QString locator = item->locator;
            if (!absoluteAssets && (item->kind == "image" || item->kind == "media") && QDir::isAbsolutePath(locator)) {
                const auto relative = QFileInfo(path).absoluteDir().relativeFilePath(locator);
                if (relative != ".." && !relative.startsWith("../") && !QDir::isAbsolutePath(relative)) locator = relative;
            }
            jsonSources.append(QJsonObject{{"id", item->id}, {"name", item->name}, {"kind", item->kind},
            {"locator", locator}, {"invite", item->invite}, {"color", item->color.name(QColor::HexArgb)}, {"gain", item->gain.load()},
            {"noiseSuppression", item->noiseSuppression.load()}, {"noiseGate", item->noiseGate.load()}, {"compression", item->compression.load()}, {"muted", item->muted.load()}, {"followsScene", item->followsScene.load()}, {"monitored", item->monitored.load()}, {"transparent", item->transparent}, {"audioDelayMs", item->audioDelayMs.load()},
            {"preserveColorMetadata", item->preserveColorMetadata},
            {"programAudio", item->programAudio.load()}, {"channelMode", item->channelMode.load()},
            {"browserWidth", item->browserWidth}, {"browserHeight", item->browserHeight},
            {"mediaRestartOnTake", item->mediaRestartOnTake},
            {"returnId", item->returnId}, {"returnMode", item->returnMode}, {"admitted", item->admitted.load()}, {"windowTitle", item->windowTitle}, {"windowExecutable", item->windowExecutable}, {"mediaLoop", item->mediaLoop}, {"mediaPaused", item->mediaPaused}, {"cameraWidth", item->cameraWidth}, {"cameraHeight", item->cameraHeight}, {"cameraFps", item->cameraFps}, {"cameraControls", item->cameraControls}});
        }
        for (const auto &scene : scenes) {
            QJsonArray layers;
            for (const auto &layer : scene.layers) layers.append(QJsonObject{{"source", layer.source}, {"x", layer.rect.x()}, {"y", layer.rect.y()},
                {"w", layer.rect.width()}, {"h", layer.rect.height()}, {"visible", layer.visible}, {"opacity", layer.opacity}, {"locked", layer.locked}, {"fit", layer.fit},
                {"cropX", layer.crop.x()}, {"cropY", layer.crop.y()}, {"cropW", layer.crop.width()}, {"cropH", layer.crop.height()}});
            jsonScenes.append(QJsonObject{{"id", scene.id}, {"name", scene.name}, {"background", scene.background.name(QColor::HexArgb)}, {"layers", layers}});
        }
        return {{"version", 1}, {"sources", jsonSources}, {"scenes", jsonScenes},
            {"recordAudioSources",QJsonArray::fromStringList(recordAudioSources)}, {"recordIsoSources",QJsonArray::fromStringList(recordIsoSources)},
            {"room", room}, {"roomPassword", roomPassword}, {"guestReturns", guestReturnEnabled->isChecked()}, {"masterGain", masterGain.load()}, {"canvasFormat", canvasFormat->currentIndex()}, {"renderer", renderer->currentIndex()}};
    }
    bool save(const QString &path) {
        auto lease = showFileAccess().reserve(path);
        if (!lease.allowed) { message("This show is open in another instance, or its folder cannot be locked."); return false; }
        return saveDocument(path);
    }
    bool saveDocument(const QString &path) {
        const auto bytes = QJsonDocument(showDocument(path)).toJson();
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile previous(path);
        if (previous.exists() && previous.open(QIODevice::ReadOnly) && previous.size() <= 2*1024*1024) {
            const auto old = previous.readAll(); previous.close();
            if (old == bytes) return true;
            const auto now = QDateTime::currentSecsSinceEpoch();
            if (now-backupTimes.value(path) >= 60 && QJsonDocument::fromJson(old).object()["version"].toInt() == 1) {
                const auto error = writeShowFile(path+".backup.mixer.json",old);
                if (!error.isEmpty()) { message("Could not back up show: "+error); return false; }
                backupTimes[path] = now;
            }
        }
        const auto error = writeShowFile(path,bytes);
        if (!error.isEmpty()) { message("Could not save show: "+error); return false; }
        return true;
    }
    bool saveAs(const QString &path) {
        if (!QFileInfo(path).isAbsolute() || !path.endsWith(".mixer.json",Qt::CaseInsensitive)) { message("Use an absolute .mixer.json path."); return false; }
        auto lease = showFileAccess().reserve(path);
        if (!lease.allowed) { message("This show is open in another instance, or its folder cannot be locked."); return false; }
        if (!save(projectPath) || !saveDocument(path)) return false;
        activateProject(std::move(lease));
        message("Active show: "+projectPath+". Later edits autosave here."); return true;
    }
    void activateProject(ShowFileAccess::Lease lease) {
        showFileAccess().activate(std::move(lease));
        projectPath = showFileAccess().path();
        if (qApp->property("studioShow").toString().isEmpty()) QSettings("VDO.Ninja","Video Mixer").setValue("mixer/activeProject",projectPath);
    }
    QString collect(const QString &path) {
        if (collectionBusy) return "A show collection is already running.";
        if (!QFileInfo(path).isAbsolute() || QFileInfo::exists(path) || !QFileInfo(path).dir().exists()) return "Choose a new absolute folder inside an existing directory.";
        collectionBusy = true; collectionStatus = "Collecting show and local assets..."; message(collectionStatus);
        const auto document = showDocument(projectPath,true);
        auto *watcher = new QFutureWatcher<QString>(q);
        QObject::connect(watcher,&QFutureWatcher<QString>::finished,q,[this,watcher,path] {
            const auto error = watcher->result(); watcher->deleteLater(); collectionBusy = false;
            collectionStatus = error.isEmpty() ? "Collected show: "+QDir(path).filePath("show.mixer.json") : error;
            message(collectionStatus);
        });
        // Future delivery is disconnected with the widget. The copy worker
        // never dereferences a UI object racing application shutdown.
        watcher->setFuture(QtConcurrent::run([document,path,cancelled=collectionCancelled] {
            try { return collectShow(document,path,*cancelled); } catch (...) { return QString("Could not collect the show."); }
        }));
        return {};
    }
    int relink(const QString &folder) {
        int count = 0;
        for (auto &item : sources) {
            if ((item->kind != "image" && item->kind != "media") || QFileInfo::exists(item->locator)) continue;
            const auto replacement = QDir(folder).absoluteFilePath(QFileInfo(item->locator).fileName());
            if (!QFileInfo(replacement).isFile()) continue;
            stopSource(item); item->locator = replacement; startSource(item); ++count;
        }
        if (count) { rebuildSources(); changed(); }
        return count;
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
            if ((item->kind == "image" || item->kind == "media") && !item->locator.isEmpty() &&
                !QDir::isAbsolutePath(item->locator) && QUrl(item->locator).scheme().isEmpty())
                item->locator = QFileInfo(path).absoluteDir().absoluteFilePath(item->locator);
            if (!item->invite.isEmpty() && !webUrl(item->invite)) { message("Invalid invite URL in show."); return false; }
            item->color = QColor(obj["color"].toString()); if (!item->color.isValid()) item->color = QColor("#183657");
            item->gain = static_cast<float>(std::clamp(obj["gain"].toDouble(1), 0.0, 2.0)); item->muted = obj["muted"].toBool(); item->followsScene = obj["followsScene"].toBool(true);
            item->noiseSuppression = obj["noiseSuppression"].toBool(); item->noiseGate = obj["noiseGate"].toBool(); item->compression = obj["compression"].toBool();
            item->monitored = obj["monitored"].toBool(item->kind != "microphone"); item->transparent = obj["transparent"].toBool(item->kind == "chat");
            item->preserveColorMetadata = obj["preserveColorMetadata"].toBool();
            item->programAudio = obj["programAudio"].toBool(true); item->channelMode = std::clamp(obj["channelMode"].toInt(), 0, 3);
            item->browserWidth = std::clamp(obj["browserWidth"].toInt(1280), 160, 3840); item->browserHeight = std::clamp(obj["browserHeight"].toInt(720), 120, 3840);
            const bool browserAudio = item->kind == "browser" || item->kind == "guest" || item->kind == "wireless";
            item->audioDelayMs = std::clamp(obj["audioDelayMs"].toInt(browserAudio ? 60 : 0), 0, 500);
            item->cameraWidth = std::clamp(obj["cameraWidth"].toInt(Width), 160, 7680);
            item->cameraHeight = std::clamp(obj["cameraHeight"].toInt(Height), 120, 4320);
            item->cameraFps = std::clamp(obj["cameraFps"].toDouble(30), 1.0, 240.0);
            item->cameraControls = obj["cameraControls"].toObject();
            item->returnId = obj["returnId"].toString().left(64); item->admitted = obj["admitted"].toBool(true);
            item->returnMode = obj["returnMode"].toString("video");
            if (item->returnMode != "none" && item->returnMode != "audio" && item->returnMode != "video") { message("Invalid guest return mode."); return false; }
            item->windowTitle = obj["windowTitle"].toString().left(512); item->windowExecutable = obj["windowExecutable"].toString().left(1024);
            item->mediaLoop = obj["mediaLoop"].toBool(true); item->mediaPaused = obj["mediaPaused"].toBool();
            item->mediaRestartOnTake = obj["mediaRestartOnTake"].toBool();
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
        recordAudioSources.clear(); recordIsoSources.clear();
        for (const auto &id : data["recordAudioSources"].toArray()) if (sourceIds.contains(id.toString()) && !recordAudioSources.contains(id.toString()) && recordAudioSources.size()<16) recordAudioSources.append(id.toString());
        for (const auto &id : data["recordIsoSources"].toArray()) if (sourceIds.contains(id.toString()) && !recordIsoSources.contains(id.toString()) && recordIsoSources.size()<4) recordIsoSources.append(id.toString());
        const int format = std::clamp(data["canvasFormat"].toInt(), 0, int(CanvasFormats.size())-1);
        canvasFormat->setCurrentIndex(format);
        renderer->setCurrentIndex(std::clamp(data["renderer"].toInt(1), 0, 1));
        Width = CanvasFormats[format].width; Height = CanvasFormats[format].height; canvasFps = CanvasFormats[format].fps;
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
        guestReturnEnabled->setChecked(data["guestReturns"].toBool());
        loading = false; refreshGuestReturns(); return true;
    }
};

MixerWidget::MixerWidget(app::VersusApp *core, QWidget *parent) : QWidget(parent), impl_(std::make_unique<Impl>(this, core)) {
    setWorkspaceLayout(QSettings("VDO.Ninja", "Video Mixer").value("ui/workspaceLayout", "balanced").toString());
}
MixerWidget::~MixerWidget() = default;
void MixerWidget::shutdown() { impl_->shutdown(); }
QSize MixerWidget::canvasSize() const { return {impl_->Width, impl_->Height}; }
int MixerWidget::canvasFrameRate() const { return impl_->canvasFps; }
QImage MixerWidget::captureCanvas(const QString &target) {
    auto &i = *impl_;
    if (target == "preview") return i.compose(i.preview(), 0);
    // Commands may change scene/format before the next output timer fires.
    // Compose current state without advancing that timer or submitting an extra
    // recording/stream frame just because an automation client took a picture.
    const auto frame = i.composeProgram();
    if (target == "window") {
        // Hidden canvases skip normal repaint work. Populate them on demand so
        // a windowless app screenshot still shows current Program/Preview.
        i.previewView->present(i.studioMode ? i.compose(i.preview(), 0) : frame);
        i.programView->present(frame);
    }
    return frame;
}
void MixerWidget::setWorkspaceLayout(const QString &layout) {
    auto &i = *impl_;
    if (layout == "podcast") i.panels->setSizes({180, 420, 700});
    else if (layout == "production") i.panels->setSizes({240, 740, 360});
    else if (layout == "simple") i.panels->setSizes({180, 580, 340});
    else i.panels->setSizes({220, 680, 380});
    auto *root = qobject_cast<QVBoxLayout*>(this->layout());
    root->setStretch(root->indexOf(i.panels), layout == "simple" ? 2 : 4);
    setProperty("workspaceLayout", layout);
    if (qApp->property("studioShow").toString().isEmpty())
        QSettings("VDO.Ninja", "Video Mixer").setValue("ui/workspaceLayout", layout);
}
QJsonObject MixerWidget::controlState() const {
    auto &i = *impl_; QJsonArray scenes, sources;
    auto layers = [](const Scene &scene) {
        QJsonArray result;
        for (const auto &l : scene.layers) result.append(QJsonObject{{"source", l.source},
            {"x", l.rect.x()}, {"y", l.rect.y()}, {"w", l.rect.width()}, {"h", l.rect.height()},
            {"visible", l.visible}, {"opacity", l.opacity}, {"locked", l.locked}, {"fit", l.fit},
            {"cropX", l.crop.x()}, {"cropY", l.crop.y()}, {"cropW", l.crop.width()}, {"cropH", l.crop.height()}});
        return result;
    };
    for (const auto &s : i.scenes) scenes.append(QJsonObject{{"id", s.id}, {"name", s.name},
        {"background", s.background.name(QColor::HexArgb)}, {"layers", layers(s)}});
    for (const auto &s : i.sources) sources.append(QJsonObject{{"id", s->id}, {"name", s->name}, {"kind", s->kind},
        {"has_audio", s->hasAudio()}, {"gain", s->gain.load()}, {"muted", s->muted.load()},
        {"followsScene", s->followsScene.load()}, {"monitored", s->monitored.load()},
        {"programAudio", s->programAudio.load()}, {"channelMode", s->channelMode.load()},
        {"browserWidth", s->browserWidth}, {"browserHeight", s->browserHeight},
        {"noiseSuppression", s->noiseSuppression.load()}, {"noiseGate", s->noiseGate.load()}, {"compression", s->compression.load()},
        {"audioDelayMs", s->audioDelayMs.load()}, {"peak", s->peak.load()}, {"on_program", s->onProgram.load()},
        {"frames", int(s->frames.load())}, {"starting", s->starting},
        {"error", s->media && !s->media->error().isEmpty() ? s->media->error() : s->error},
        {"ended", s->media && s->media->ended()},
        {"transparent", s->transparent}, {"preserveColorMetadata", s->preserveColorMetadata},
        {"returnMode", s->returnMode}, {"admitted", s->admitted.load()},
        {"paused", s->mediaPaused}, {"loop", s->mediaLoop}, {"restartOnTake",s->mediaRestartOnTake},
        {"positionMs",s->media ? qint64(s->media->positionMs()) : 0}, {"durationMs",s->media ? qint64(s->media->durationMs()) : 0}});
    QJsonArray monitorDevices;
    QJsonArray isoRecordings;
    for (const auto &iso : i.recordingPlan.load()->isos) isoRecordings.append(QJsonObject{{"source",iso.id},{"active",iso.recorder->active()},{"busy",iso.recorder->busy()},{"status",iso.recorder->status()},{"path",iso.recorder->path()}});
    for (int n = 0; n < i.monitorDevice->count(); ++n) monitorDevices.append(QJsonObject{
        {"id", i.monitorDevice->itemData(n, Qt::UserRole+1).toString()}, {"name", i.monitorDevice->itemText(n)}, {"available", i.monitorDevice->itemData(n).toInt() >= -1}});
    return {{"scenes", scenes}, {"sources", sources}, {"preview", i.preview().id}, {"program", i.program.id},
        {"program_layers", layers(i.program)}, {"transitioning", i.fading},
        {"mode", i.studioMode ? "studio" : "single"}, {"layout", property("workspaceLayout").toString()},
        {"canvas", QJsonObject{{"width", i.Width}, {"height", i.Height}, {"fps", i.canvasFps},
            {"format", i.canvasFormat->currentIndex()}, {"renderer", i.renderer->currentIndex() == 0 ? "gpu" : "cpu"},
            {"active_renderer", i.programRenderer}, {"renderer_error", i.compositorError}}},
        {"recording", QJsonObject{{"active", i.recordingActive()}, {"busy", i.recordingBusy()}, {"program_active",i.recorder.active()},
            {"audio_sources",QJsonArray::fromStringList(i.recordingPlan.load()->audioIds)}, {"separate_videos",isoRecordings},
            {"status", i.recorder.status()}, {"dropped_frames", qint64(i.recorder.dropped())}, {"remaining_seconds_estimate",qint64(i.recorder.remainingSeconds())}, {"exporting",bool(i.exportProcess)}, {"export_status",i.exportStatus}}},
        {"monitor", QJsonObject{{"enabled", i.monitorEnable->isChecked()}, {"device", i.monitorDevice->currentData(Qt::UserRole+1).toString()}, {"gain", i.monitorGain.load()}, {"refreshing", i.monitorDevicesRefreshing}, {"devices", monitorDevices}}},
        {"private_break", i.privateBreak.load()}, {"microphones_muted", i.microphonesMuted.load()}, {"private_talkback", bool(i.privateTalkback || i.heldTalkback)},
        {"project_path", i.projectPath}, {"show_collection",QJsonObject{{"busy",i.collectionBusy},{"status",i.collectionStatus}}},
        {"browser_resource_budget",ProcessWorker::browserBudgetState()},
        {"local_outputs",QJsonObject{{"spout",i.spoutOutput->isChecked()},{"virtual_camera",i.virtualCamera->isChecked()},
            {"configuring",!i.virtualCamera->isEnabled()},{"status",i.localOutputs.status()}}},
        {"master_gain", i.masterGain.load()}, {"master_peak", i.masterPeak.load()}};
}
QJsonObject MixerWidget::controlCommand(const QJsonObject &c) {
    auto &i = *impl_;
    const auto command = c["command"].toString();
    const auto fail = [](const QString &why) { return QJsonObject{{"error", why}}; };
    if (!i.running) return fail("Mixer is shutting down");
    const auto sceneIndex = [&](const QString &id) {
        for (int n = 0; n < int(i.scenes.size()); ++n) if (i.scenes[n].id == id) return n;
        return -1;
    };
    const auto outputsBusy = [&] { return i.core->isLive() || i.recordingBusy() || i.spoutOutput->isChecked() ||
        i.virtualCamera->isChecked() || i.guestReturnEnabled->isChecked(); };
    QJsonObject result{{"ok", true}, {"command", command}};
    if (command == "scene.add") {
        if (i.scenes.size() >= 64) return fail("A show supports at most 64 scenes");
        Scene s; s.name = c["name"].toString().trimmed();
        if (c.contains("background")) { s.background = QColor(c["background"].toString()); if (!s.background.isValid()) return fail("Invalid color"); }
        result["id"] = s.id; i.scenes.push_back(s); i.selectedScene = int(i.scenes.size())-1;
        i.rebuild(); i.changed();
    } else if (command == "scene.take") {
        i.takeScene(c["transition"] == "fade");
    } else if (command.startsWith("scene.")) {
        const int n = sceneIndex(c["id"].toString()); if (n < 0) return fail("Scene not found");
        if (command == "scene.select") { i.selectedScene = n; }
        else if (command == "scene.rename") i.scenes[n].name = c["name"].toString().trimmed();
        else if (command == "scene.background") {
            QColor color(c["color"].toString()); if (!color.isValid()) return fail("Invalid color"); i.scenes[n].background = color;
        } else if (command == "scene.remove") {
            if (i.scenes.size() == 1) return fail("Keep at least one scene");
            i.scenes.erase(i.scenes.begin()+n); i.selectedScene = std::min(i.selectedScene, int(i.scenes.size())-1);
        } else return fail("Unknown scene command");
        i.rebuild(); i.changed();
    } else if (command == "guest.invite") {
        auto s = i.createInvite(c["kind"] == "wireless", c["name"].toString()); if (!s) return fail("A show supports at most 32 sources");
        if (c.contains("returnMode")) s->returnMode = c["returnMode"].toString();
        if (c.contains("programAudio")) s->programAudio = c["programAudio"].toBool();
        i.updateInviteReturn(s); i.appendSource(s);
        if (s->returnMode != "none") i.guestReturnEnabled->setChecked(true);
        i.refreshGuestReturns(); result["id"] = s->id; result["invite"] = s->invite;
    } else if (command == "guest.set") {
        auto s = i.source(c["id"].toString()); if (!s || (s->kind != "guest" && s->kind != "wireless")) return fail("Invited source not found");
        if (c.contains("returnMode")) {
            s->returnMode = c["returnMode"].toString(); i.updateInviteReturn(s);
            if (s->returnMode != "none") i.guestReturnEnabled->setChecked(true);
        }
        if (c.contains("admitted") && s->admitted != c["admitted"].toBool()) {
            s->admitted = c["admitted"].toBool();
            if (i.studioMode || !s->admitted) for (auto *scene : {&i.program, &i.previous}) for (auto &layer : scene->layers) if (layer.source == s->id) layer.visible = false;
            if (s->admitted) {
                auto &layers = i.preview().layers; auto found = std::find_if(layers.begin(),layers.end(),[&](const auto &l) { return l.source == s->id; });
                if (found == layers.end()) layers.push_back({s->id}); else found->visible = true;
            }
        }
        i.refreshGuestReturns(); i.updateAudioRouting(); i.rebuildSources(); i.changed();
        result["invite"] = s->invite;
    } else if (command == "source.add" || command == "source.update") {
        const bool add = command == "source.add";
        auto s = add ? std::make_shared<Source>() : i.source(c["id"].toString());
        if (!s) return fail("Source not found");
        if (add && i.sources.size() >= 32) return fail("A show supports at most 32 sources");
        const auto kind = add ? c["kind"].toString() : s->kind;
        const auto locator = c.contains("locator") ? c["locator"].toString() : s->locator;
        if ((kind == "browser" || kind == "chat" || kind == "guest" || kind == "wireless") && !webUrl(locator)) return fail("Browser sources require an HTTP or HTTPS URL");
        if ((kind == "image" || kind == "media") && (!QFileInfo(locator).isAbsolute() || !QFileInfo(locator).isFile())) return fail("Select an existing absolute file path");
        if (kind == "color" && !QColor(locator).isValid()) return fail("Invalid color");
        if ((kind == "camera" || kind == "window" || kind == "spout") && locator.isEmpty()) return fail("Select a device ID from /sources");
        const bool transparent = c.contains("transparent") ? c["transparent"].toBool() : add ? (kind == "browser" || kind == "chat") : s->transparent;
        const bool metadata = c.contains("preserveColorMetadata") ? c["preserveColorMetadata"].toBool() : s->preserveColorMetadata;
        const int browserWidth = c.contains("browserWidth") ? c["browserWidth"].toInt() : s->browserWidth;
        const int browserHeight = c.contains("browserHeight") ? c["browserHeight"].toInt() : s->browserHeight;
        const bool restart = !add && (locator != s->locator || transparent != s->transparent || metadata != s->preserveColorMetadata || browserWidth != s->browserWidth || browserHeight != s->browserHeight);
        if (restart) { s->browserRestarts.clear(); i.stopSource(s); }
        if (add) s->kind = kind;
        if (locator != s->locator && kind == "window") { s->windowTitle.clear(); s->windowExecutable.clear(); }
        if (locator != s->locator && kind == "camera") s->cameraControls = {};
        s->locator = locator; s->transparent = transparent; s->preserveColorMetadata = metadata;
        s->browserWidth = browserWidth; s->browserHeight = browserHeight;
        if (c.contains("name")) s->name = c["name"].toString().trimmed();
        if (kind == "color") s->color = QColor(locator);
        if (add) {
            if (kind == "browser") s->audioDelayMs = 60;
            if (kind == "microphone" || kind == "desktop" || kind == "guest") s->followsScene = false;
            if (kind == "microphone") s->monitored = false;
            i.appendSource(s);
        } else { if (restart) i.startSource(s); i.rebuild(); i.changed(); }
        result["id"] = s->id;
    } else if (command == "source.remove" || command == "source.reconnect") {
        auto s = i.source(c["id"].toString()); if (!s) return fail("Source not found");
        if (command == "source.remove") {
            i.selectedSource = s->id; i.removeSource();
            if (i.source(s->id)) return fail("Take the source off Program before removing it");
        } else { s->browserRestarts.clear(); i.stopSource(s); i.startSource(s); }
    } else if (command.startsWith("layer.")) {
        auto s = i.source(c["source"].toString()); if (!s) return fail("Source not found");
        const int n = c.contains("scene") ? sceneIndex(c["scene"].toString()) : i.selectedScene;
        if (n < 0) return fail("Scene not found");
        auto &layers = i.scenes[n].layers;
        auto found = std::find_if(layers.begin(), layers.end(), [&](const auto &l) { return l.source == s->id; });
        if (command == "layer.order") {
            if (found == layers.end() || c["index"].toInt() >= int(layers.size())) return fail("Layer or index not found");
            if (found->locked) return fail("Unlock the layer before moving it");
            const auto copy = *found; layers.erase(found); layers.insert(layers.begin()+c["index"].toInt(), copy);
        } else {
            Layer l = found == layers.end() ? Layer{s->id} : *found;
            if (l.locked && (!c.contains("locked") || c["locked"].toBool())) return fail("Unlock the layer before editing it");
            auto number = [&](const char *key, double old) { return c.contains(key) ? c[key].toDouble() : old; };
            l.rect = {number("x",l.rect.x()), number("y",l.rect.y()), number("w",l.rect.width()), number("h",l.rect.height())};
            l.crop = {number("cropX",l.crop.x()), number("cropY",l.crop.y()), number("cropW",l.crop.width()), number("cropH",l.crop.height())};
            if (l.crop.right() > 1.00001 || l.crop.bottom() > 1.00001) return fail("Crop extends beyond the source");
            l.opacity = number("opacity", l.opacity);
            if (c.contains("visible")) l.visible = c["visible"].toBool();
            if (c.contains("locked")) l.locked = c["locked"].toBool();
            if (c.contains("fit")) l.fit = c["fit"].toString();
            if (found == layers.end()) layers.push_back(l); else *found = l;
        }
        i.rebuildSources(); i.syncInspector(); i.changed();
    } else if (command == "production.set") {
        if (c.contains("privateBreak")) i.breakButton->setChecked(c["privateBreak"].toBool());
        if (c.contains("microphonesMuted")) i.microphonesButton->setChecked(c["microphonesMuted"].toBool());
        if (c.contains("privateTalkback")) findChild<QPushButton*>("guestTalkback")->setChecked(c["privateTalkback"].toBool());
        if (c.contains("guestReturns")) i.guestReturnEnabled->setChecked(c["guestReturns"].toBool());
    } else if (command == "local.set") {
        if ((c.contains("virtualCamera") || c["unregisterCamera"].toBool()) && !i.virtualCamera->isEnabled()) return fail("Virtual-camera registration is still running");
        if (c["virtualCamera"].toBool() && c["unregisterCamera"].toBool()) return fail("Cannot enable and unregister the virtual camera together");
        if (c.contains("spout")) i.spoutOutput->setChecked(c["spout"].toBool());
        if (c.contains("virtualCamera")) i.virtualCamera->setChecked(c["virtualCamera"].toBool());
        if (c["unregisterCamera"].toBool()) findChild<QPushButton*>("unregisterCamera")->click();
    } else if (command == "monitor.set") {
        if (c["refresh"].toBool()) i.refreshMonitorDevices();
        if (c.contains("device")) {
            const int index = i.monitorDevice->findData(c["device"].toString(), Qt::UserRole+1);
            if (index < 0 || i.monitorDevice->itemData(index).toInt() < -1) return fail("Monitoring output device is unavailable");
            i.monitorDevice->setCurrentIndex(index);
        }
        if (c.contains("gain")) findChild<QSlider*>("monitorVolume")->setValue(qRound(c["gain"].toDouble()*100));
        if (c.contains("enabled")) i.monitorEnable->setChecked(c["enabled"].toBool());
    } else if (command == "audio.set") {
        if (c["source"] == "master") {
            if (c.size() != 3 || !c.contains("gain") || c["gain"].toDouble() > 1.5) return fail("Master accepts only gain from 0 to 1.5");
            findChild<QSlider*>("masterGain")->setValue(qRound(c["gain"].toDouble()*100));
        } else {
            auto s = i.source(c["source"].toString()); if (!s || !s->hasAudio()) return fail("Audio source not found");
            if (c.contains("gain")) s->gain = float(c["gain"].toDouble());
            if (c.contains("muted")) s->muted = c["muted"].toBool();
            if (c.contains("followsScene")) s->followsScene = c["followsScene"].toBool();
            if (c.contains("monitored")) s->monitored = c["monitored"].toBool();
            if (c.contains("noiseSuppression")) s->noiseSuppression = c["noiseSuppression"].toBool();
            if (c.contains("noiseGate")) s->noiseGate = c["noiseGate"].toBool();
            if (c.contains("compression")) s->compression = c["compression"].toBool();
            if (c.contains("audioDelayMs")) s->audioDelayMs = c["audioDelayMs"].toInt();
            if (c.contains("programAudio")) s->programAudio = c["programAudio"].toBool();
            if (c.contains("channelMode")) s->channelMode = c["channelMode"].toInt();
            i.rebuildAudio(); i.changed();
        }
    } else if (command == "media.set") {
        auto s = i.source(c["id"].toString()); if (!s || !s->media) return fail("Active media source not found");
        if (c["restart"].toBool()) { s->mediaPaused = false; s->media->restart(); }
        if (c.contains("paused")) { s->mediaPaused = c["paused"].toBool(); s->media->setPaused(s->mediaPaused); }
        if (c.contains("loop")) { s->mediaLoop = c["loop"].toBool(); s->media->setLoop(s->mediaLoop); }
        if (c.contains("restartOnTake")) s->mediaRestartOnTake = c["restartOnTake"].toBool();
        if (c.contains("seekMs")) s->media->seek(c["seekMs"].toInteger());
        i.changed();
    } else if (command == "record.export") {
        const auto error = i.exportRecording(c["input"].toString(),c["path"].toString()); if (!error.isEmpty()) return fail(error);
    } else if (command == "record.start") {
        const QString path = c["path"].toString();
        if (i.recordingBusy()) return fail("Recording is already active or finalizing");
        if (!QFileInfo(path).isAbsolute() || !path.endsWith(".mkv", Qt::CaseInsensitive) || QFileInfo::exists(path)) return fail("Use a new absolute .mkv path");
        QStringList audioIds = i.recordAudioSources,isoIds = i.recordIsoSources;
        if (c.contains("audioSources")) { audioIds.clear(); for (const auto &id : c["audioSources"].toArray()) audioIds.append(id.toString()); }
        if (c.contains("isoSources")) { isoIds.clear(); for (const auto &id : c["isoSources"].toArray()) isoIds.append(id.toString()); }
        const auto error = i.startRecording(path,audioIds,isoIds); if (!error.isEmpty()) return fail(error);
    } else if (command == "record.stop") {
        i.stopRecording();
    } else if (command == "show.collect") {
        const auto error = i.collect(c["path"].toString()); if (!error.isEmpty()) return fail(error);
    } else if (command == "show.relink") {
        const auto folder = c["path"].toString();
        if (!QFileInfo(folder).isAbsolute() || !QFileInfo(folder).isDir()) return fail("Choose an existing absolute folder");
        result["relinked"] = i.relink(folder);
    } else if (command == "show.save" || command == "show.saveAs" || command == "show.open") {
        const QString path = c["path"].toString();
        if (!QFileInfo(path).isAbsolute() || !path.endsWith(".mixer.json", Qt::CaseInsensitive)) return fail("Use an absolute .mixer.json path");
        if (command == "show.save") { if (!i.save(path)) return fail(i.status->text()); }
        else if (command == "show.saveAs") { if (!i.saveAs(path)) return fail(i.status->text()); }
        else {
            if (outputsBusy()) return fail("Stop all outputs before opening a show");
            auto lease = c["activate"].toBool() ? showFileAccess().reserve(path) : ShowFileAccess::Lease{};
            if (c["activate"].toBool() && !lease.allowed) return fail("This show is open in another instance, or its folder cannot be locked");
            if (c["activate"].toBool() && !i.save(i.projectPath)) return fail(i.status->text());
            if (!i.load(path)) return fail("Could not load show");
            if (c["activate"].toBool()) i.activateProject(std::move(lease));
            i.selectedScene = 0; i.selectedSource.clear(); i.program = i.scenes[0]; i.fading = false;
            i.history.clear(); i.historyIndex = 0; i.rebuild(); i.updateAudioRouting(); i.changed();
        }
    } else if (command == "workspace.set") {
        if (c.contains("mode")) findChild<QCheckBox*>("studioMode")->setChecked(c["mode"] == "studio");
        if (c.contains("layout")) setWorkspaceLayout(c["layout"].toString());
    } else if (command == "canvas.set") {
        if (outputsBusy()) return fail("Stop all outputs before changing the canvas");
        if (c.contains("format")) {
            const int format = c["format"].toInt(-1);
            if (format < 0 || format >= int(CanvasFormats.size())) return fail("Unknown canvas format");
            i.canvasFormat->setCurrentIndex(format);
        }
        if (c.contains("renderer")) i.renderer->setCurrentIndex(c["renderer"] == "gpu" ? 0 : 1);
    } else return fail("Unknown mixer command");
    return result;
}
void MixerWidget::setOutputState(const QString &label, const QString &status, bool enabled, bool active) {
    impl_->startOutput->setText(label); impl_->startOutput->setAccessibleName(label);
    impl_->startOutput->setEnabled(enabled); impl_->recordOutput->setEnabled(!impl_->recordingBusy() || impl_->recordingActive());
    impl_->outputStatus->setText(active ? status : QString(status).replace("Select a window to capture", "Mixer ready"));
}
} // namespace versus::mixer
