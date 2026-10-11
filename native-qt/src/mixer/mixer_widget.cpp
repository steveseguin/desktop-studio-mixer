#include "versus/mixer/mixer_widget.h"
#include "versus/mixer/browser_source.h"
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
#include <QProgressBar>
#include <QPushButton>
#include <QSaveFile>
#include <QScrollArea>
#include <QShortcut>
#include <QSlider>
#include <QSplitter>
#include <QStandardPaths>
#include <QTimer>
#include <QUrlQuery>
#include <QUuid>
#include <algorithm>
#include <cmath>
#include <deque>
#include <mutex>
#include <thread>

namespace versus::mixer {
namespace {
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
};
struct Scene {
    QString id = newId(), name;
    QColor background{"#101722"};
    std::vector<Layer> layers;
};
struct Source {
    QString id = newId(), name, kind, locator, invite, error;
    QColor color{"#183657"};
    std::mutex imageMutex, audioMutex;
    QImage image;
    std::deque<float> samples;
    audio::AudioResamplerState resampler;
    std::atomic<float> gain{1}, peak{0};
    std::atomic<bool> muted{false}, followsScene{true}, onProgram{false};
    std::unique_ptr<video::WindowCapture> window;
    std::unique_ptr<video::CameraCapture> camera;
    std::unique_ptr<video::SpoutCapture> spout;
    std::unique_ptr<audio::WindowAudioCaptureCore> audio;
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
            kind == "browser" || kind == "guest" || kind == "wireless";
    }
};

class Canvas : public QWidget {
public:
    QImage image;
    QRectF selection;
    std::function<void(QPointF)> pressed;
    std::function<void(QPointF)> dragged;
    explicit Canvas(QWidget *parent = nullptr) : QWidget(parent) {
        setMinimumSize(240, 135);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    }
    QRectF viewport() const {
        QSizeF size(Width, Height); size.scale(this->size(), Qt::KeepAspectRatio);
        return {(width() - size.width()) / 2, (height() - size.height()) / 2, size.width(), size.height()};
    }
    QPointF relative(QPointF point) const {
        const auto view = viewport();
        return {(point.x() - view.x()) / view.width(), (point.y() - view.y()) / view.height()};
    }
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.fillRect(rect(), QColor("#080b11"));
        painter.drawImage(viewport(), image);
        if (!selection.isEmpty()) {
            const auto view = viewport();
            painter.setPen(QPen(QColor("#7ca8ff"), 2));
            painter.drawRect(QRectF(view.x() + selection.x()*view.width(), view.y() + selection.y()*view.height(),
                selection.width()*view.width(), selection.height()*view.height()));
        }
    }
    void mousePressEvent(QMouseEvent *event) override {
        if (event->button() == Qt::LeftButton && pressed) pressed(relative(event->position()));
    }
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
    QTimer videoTimer, saveTimer;
    QElapsedTimer transition;
    bool fading = false, rebuilding = false, loading = false;
    std::mutex sourcesMutex;
    std::vector<std::shared_ptr<Source>> sources;
    std::vector<Scene> scenes;
    Scene program, previous;
    int selectedScene = 0;
    QString selectedSource;
    QPointF dragStart;
    QRectF dragRect;
    QListWidget *sceneList = nullptr, *sourceList = nullptr;
    Canvas *previewView = nullptr, *programView = nullptr;
    QPointer<Canvas> outputView;
    QPointer<BrowserSource> chatDock;
    QLabel *status = nullptr, *previewTitle = nullptr, *programTitle = nullptr;
    QVBoxLayout *audioRows = nullptr;
    std::vector<std::pair<std::weak_ptr<Source>, QProgressBar*>> meters;
    QProgressBar *masterMeter = nullptr;
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
        load(projectPath);
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
        rebuild(); updateAudioRouting();
        saveTimer.setSingleShot(true);
        QObject::connect(&saveTimer, &QTimer::timeout, q, [this] { save(projectPath); });
        videoTimer.setTimerType(Qt::PreciseTimer);
        QObject::connect(&videoTimer, &QTimer::timeout, q, [this] { render(); });
        videoTimer.start(33);
        audioThread = std::thread([this] { mixAudio(); });
    }
    ~Impl() {
        videoTimer.stop(); saveTimer.stop(); save(projectPath);
        running = false;
        if (audioThread.joinable()) audioThread.join();
        delete outputView.data();
        delete chatDock.data();
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
    void changed() { if (!loading) saveTimer.start(500); }
    void message(const QString &text) { status->setText(text); }

    void buildUi() {
        q->setStyleSheet("QPushButton { background:#192b3d; color:#eaf4ff; border:1px solid #304358; border-radius:4px; padding:8px; }"
            "QPushButton:hover { background:#253c52; } QPushButton#takeScene, QPushButton#mixerOutput { background:#00c2ff; color:#06131a; font-weight:700; }"
            "QListWidget::indicator { width:14px; height:14px; border:1px solid #647b91; border-radius:3px; }"
            "QListWidget::indicator:checked { background:#00c2ff; } QListWidget::indicator:unchecked { background:#101b27; }");
        auto *root = new QVBoxLayout(q); root->setContentsMargins(18, 14, 18, 14); root->setSpacing(12);
        auto *toolbar = new QHBoxLayout;
        auto *brand = new QLabel("<b style='font-size:22px'>Video Mixer</b><br>Build a scene. Preview it. Take it live.", q);
        toolbar->addWidget(brand); toolbar->addStretch();
        auto *invite = button("Invite guest", "inviteGuest", toolbar, q);
        auto *wireless = button("Wireless camera", "inviteCamera", toolbar, q);
        auto *chat = button("Social chat", "socialChat", toolbar, q);
        auto *output = button("Output settings", "mixerOutput", toolbar, q);
        QObject::connect(invite, &QPushButton::clicked, q, [this] { addInvite(false); });
        QObject::connect(wireless, &QPushButton::clicked, q, [this] { addInvite(true); });
        QObject::connect(chat, &QPushButton::clicked, q, [this] { addChat(); });
        QObject::connect(output, &QPushButton::clicked, q, [this] { emit q->outputRequested(); });
        root->addLayout(toolbar);
        auto *monitors = new QHBoxLayout;
        auto monitor = [&](const QString &name, Canvas *&view, QLabel *&title) {
            auto *box = new QVBoxLayout;
            title = new QLabel(name, q); title->setStyleSheet("font-weight:700; color:#9ab5e8");
            box->addWidget(title); view = new Canvas(q); view->setObjectName(name.toLower()+"Canvas");
            box->addWidget(view, 1); monitors->addLayout(box, 1);
        };
        monitor("Preview", previewView, previewTitle);
        monitor("Program", programView, programTitle);
        root->addLayout(monitors, 5);
        auto *transport = new QHBoxLayout;
        auto *take = button("Take / Cut", "takeScene", transport, q);
        auto *fade = button("Fade (400 ms)", "fadeScene", transport, q);
        transport->addStretch();
        auto *popout = button("Program window", "programWindow", transport, q);
        auto *exportShow = button("Save show as...", "exportShow", transport, q);
        auto *importShow = button("Open show...", "importShow", transport, q);
        QObject::connect(take, &QPushButton::clicked, q, [this] { takeScene(false); });
        QObject::connect(fade, &QPushButton::clicked, q, [this] { takeScene(true); });
        QObject::connect(popout, &QPushButton::clicked, q, [this] {
            if (!outputView) {
                outputView = new Canvas;
                outputView->setWindowTitle("Video Mixer - Program");
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
            if (core->isLive()) { message("Stop output before opening another show."); return; }
            const auto path = QFileDialog::getOpenFileName(q, "Open show", {}, "Mixer show (*.mixer.json)");
            if (!path.isEmpty() && load(path)) {
                selectedScene = 0; selectedSource.clear(); program = scenes[0]; fading = false;
                rebuild(); updateAudioRouting(); changed();
            }
        });
        root->addLayout(transport);

        auto *panels = new QSplitter(Qt::Horizontal, q);
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
            bool ok; const auto name = QInputDialog::getText(q, "New scene", "Scene name", QLineEdit::Normal, "Scene " + QString::number(scenes.size()+1), &ok);
            if (!ok || name.trimmed().isEmpty()) return;
            Scene scene; scene.name = name.trimmed(); scenes.push_back(scene); selectedScene = static_cast<int>(scenes.size())-1; rebuild(); changed();
        });
        QObject::connect(duplicate, &QPushButton::clicked, q, [this] {
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

        auto *sourceBox = new QGroupBox("Sources - checked items appear in Preview", panels);
        auto *sourceLayout = new QVBoxLayout(sourceBox);
        sourceList = new QListWidget(sourceBox); sourceList->setObjectName("mixerSources"); sourceLayout->addWidget(sourceList);
        auto *sourceButtons = new QHBoxLayout;
        auto *add = button("+ Add source", "addSource", sourceButtons, q);
        auto *remove = button("Remove", "removeSource", sourceButtons, q);
        auto *interact = button("Open / Invite", "interactSource", sourceButtons, q);
        sourceLayout->addLayout(sourceButtons);
        QObject::connect(add, &QPushButton::clicked, q, [this] { addSourceDialog(); });
        QObject::connect(remove, &QPushButton::clicked, q, [this] { removeSource(); });
        QObject::connect(interact, &QPushButton::clicked, q, [this] {
            auto item = source(selectedSource); if (!item) return;
            if (!item->invite.isEmpty()) showInvite(item);
            else if (item->kind == "chat" && QUrlQuery(QUrl(item->locator)).hasQueryItem("session")) {
                QUrl dock("https://socialstream.ninja/dock.html");
                QUrlQuery query;
                query.addQueryItem("session", QUrlQuery(QUrl(item->locator)).queryItemValue("session", QUrl::FullyDecoded));
                dock.setQuery(query);
                delete chatDock.data();
                chatDock = new BrowserSource("chat-dock", dock, false);
                chatDock->setWindowTitle("Video Mixer - Social Stream chat dock");
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
                if (auto *layer = selectedLayer()) {
                    layer->rect = QRectF(x->value()/100, y->value()/100, w->value()/100, h->value()/100); changed();
                }
            });
        };
        makeCoordinate("X", x); makeCoordinate("Y", y); makeCoordinate("W", w); makeCoordinate("H", h);
        sourceLayout->addLayout(geometry);
        auto *layerButtons = new QHBoxLayout;
        auto *front = button("Bring forward", "layerForward", layerButtons, q);
        auto *back = button("Send back", "layerBack", layerButtons, q);
        layerButtons->addWidget(new QLabel("Opacity", q));
        opacity = new QSlider(Qt::Horizontal, q); opacity->setRange(0, 100); opacity->setValue(100); layerButtons->addWidget(opacity);
        sourceLayout->addLayout(layerButtons);
        QObject::connect(front, &QPushButton::clicked, q, [this] { moveLayer(1); });
        QObject::connect(back, &QPushButton::clicked, q, [this] { moveLayer(-1); });
        QObject::connect(opacity, &QSlider::valueChanged, q, [this](int value) {
            if (!rebuilding) if (auto *layer = selectedLayer()) { layer->opacity = value/100.0; changed(); }
        });
        previewView->pressed = [this](QPointF point) {
            for (auto it = preview().layers.rbegin(); it != preview().layers.rend(); ++it) {
                if (it->visible && it->rect.contains(point)) {
                    selectedSource = it->source; dragRect = it->rect; dragStart = point;
                    rebuildSources(); syncInspector(); break;
                }
            }
        };
        previewView->dragged = [this](QPointF point) {
            if (auto *layer = selectedLayer()) {
                const auto delta = point-dragStart;
                layer->rect.moveTopLeft({std::clamp(dragRect.x()+delta.x(), 0.0, std::max(0.0, 1-dragRect.width())),
                    std::clamp(dragRect.y()+delta.y(), 0.0, std::max(0.0, 1-dragRect.height()))});
                syncInspector(); changed();
            }
        };

        auto *audioBox = new QGroupBox("Audio mixer", panels); auto *audioLayout = new QVBoxLayout(audioBox);
        auto *scroll = new QScrollArea(audioBox); scroll->setWidgetResizable(true);
        auto *audioWidget = new QWidget(scroll); audioRows = new QVBoxLayout(audioWidget); scroll->setWidget(audioWidget);
        audioLayout->addWidget(scroll);
        auto *master = new QHBoxLayout; master->addWidget(new QLabel("Master", q));
        auto *masterSlider = new QSlider(Qt::Horizontal, q); masterSlider->setRange(0, 150); masterSlider->setValue(100);
        masterSlider->setObjectName("masterGain"); master->addWidget(masterSlider);
        masterMeter = new QProgressBar(q); masterMeter->setRange(0, 100); masterMeter->setTextVisible(false); masterMeter->setMaximumWidth(90); master->addWidget(masterMeter);
        audioLayout->addLayout(master);
        QObject::connect(masterSlider, &QSlider::valueChanged, q, [this](int value) { masterGain = value/100.0f; });
        audioLayout->addWidget(new QLabel("Scene = audio follows Program.\nUncheck it for an always-on mic. Peak limiter enabled.", q));
        panels->setSizes({240, 600, 370}); root->addWidget(panels, 4);
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
            row->setToolTip(item->error.isEmpty() ? "Toggle to include in Preview. Take to update Program." : item->error);
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
        for (auto *spin : {x, y, w, h}) spin->setEnabled(layer != nullptr);
        opacity->setEnabled(layer != nullptr);
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
            auto *row = new QWidget(q); auto *layout = new QVBoxLayout(row); layout->setContentsMargins(0, 0, 0, 6);
            layout->addWidget(new QLabel(item->name, row));
            auto *controls = new QHBoxLayout;
            auto *gain = new QSlider(Qt::Horizontal, row); gain->setRange(0, 200); gain->setValue(qRound(item->gain*100));
            gain->setAccessibleName(item->name+" volume"); controls->addWidget(gain);
            auto *mute = new QCheckBox("Mute", row); mute->setChecked(item->muted); controls->addWidget(mute);
            auto *follow = new QCheckBox("Scene", row); follow->setChecked(item->followsScene); controls->addWidget(follow);
            auto *meter = new QProgressBar(row); meter->setRange(0, 100); meter->setTextVisible(false); meter->setMaximumWidth(65); controls->addWidget(meter);
            meters.emplace_back(item, meter);
            layout->addLayout(controls); audioRows->addWidget(row);
            QObject::connect(gain, &QSlider::valueChanged, q, [this, item](int value) { item->gain = value/100.0f; changed(); });
            QObject::connect(mute, &QCheckBox::toggled, q, [this, item](bool value) { item->muted = value; changed(); });
            QObject::connect(follow, &QCheckBox::toggled, q, [this, item](bool value) { item->followsScene = value; changed(); });
        }
        audioRows->addStretch();
    }

    void addSourceDialog() {
        QDialog dialog(q); dialog.setWindowTitle("Add source"); auto *form = new QFormLayout(&dialog);
        QComboBox type; type.setObjectName("sourceKind");
        const std::vector<std::pair<QString, QString>> kinds = {{"Game / window", "window"}, {"Camera / webcam", "camera"},
            {"Microphone / audio input", "microphone"}, {"Desktop audio", "desktop"}, {"Spout2 feed", "spout"},
            {"Browser / remote VDO.Ninja feed", "browser"}, {"Image", "image"}, {"Title / slate", "title"}, {"Color", "color"}};
        for (const auto &[label, value] : kinds) type.addItem(label, value);
        QLineEdit name; name.setObjectName("sourceName"); name.setPlaceholderText("Optional source name");
        QComboBox device; device.setObjectName("sourceDevice"); device.setMinimumWidth(400);
        QLineEdit value; value.setObjectName("sourceValue");
        QPushButton browse("Choose file / color");
        QLabel help; help.setWordWrap(true);
        form->addRow("Source type", &type); form->addRow("Name", &name); form->addRow("Device / window", &device);
        form->addRow("URL / file / text / color", &value); form->addRow(&browse); form->addRow(&help);
        auto populate = [&] {
            const auto kind = type.currentData().toString(); device.clear();
            value.clear(); value.setVisible(true); device.setVisible(true); browse.setVisible(kind == "image" || kind == "color");
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
                if (kind == "desktop") { value.setVisible(false); help.setText("Captures the default Windows output, including browser monitoring. Avoid adding it alongside the same individual audio feeds."); }
                if (kind == "browser") { value.setPlaceholderText("https://..."); help.setText("Use a website, browser overlay, or VDO.Ninja viewing URL. Browser video currently captures up to 15 fps. Open / Invite allows interaction."); }
                if (kind == "image") help.setText("PNG, JPEG and other Qt-supported images. Transparent PNGs remain transparent.");
                if (kind == "title") { value.setText("Your title"); help.setText("A full-frame title card. Resize and position it in Preview."); }
                if (kind == "color") { value.setText("#24466f"); help.setText("A solid color layer."); }
            }
        };
        QObject::connect(&type, &QComboBox::currentIndexChanged, &dialog, populate);
        QObject::connect(&browse, &QPushButton::clicked, &dialog, [&] {
            if (type.currentData() == "image") {
                const auto file = QFileDialog::getOpenFileName(&dialog, "Choose image", {}, "Images (*.png *.jpg *.jpeg *.webp *.bmp)");
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
            if (kind == "browser" && !webUrl(value.text().trimmed())) { help.setText("Enter a complete http:// or https:// source URL."); return; }
            if (kind == "color" && !QColor(value.text()).isValid()) { help.setText("Enter a valid color, for example #24466f."); return; }
            if (kind == "image" && !QImageReader(value.text()).canRead()) { help.setText("Choose a readable image file."); return; }
            auto item = std::make_shared<Source>(); item->kind = kind;
            item->locator = device.isVisible() ? device.currentData().toString() : value.text().trimmed();
            item->name = name.text().trimmed();
            if (item->name.isEmpty()) item->name = device.isVisible() ? device.currentText() : type.currentText();
            if (kind == "color") item->color = QColor(item->locator);
            if (kind == "microphone" || kind == "desktop") item->followsScene = false;
            appendSource(item); dialog.accept();
        });
        populate(); dialog.exec();
    }

    void appendSource(const std::shared_ptr<Source> &item) {
        if (sources.size() >= 32) { message("This version supports up to 32 sources per show."); return; }
        { std::lock_guard lock(sourcesMutex); sources.push_back(item); }
        preview().layers.push_back({item->id}); selectedSource = item->id;
        startSource(item); rebuild(); changed();
    }

    void startSource(const std::shared_ptr<Source> &item) {
        const std::weak_ptr<Source> weak = item;
        auto frame = [weak](video::CapturedFrame captured) {
            const auto item = weak.lock();
            if (!item || captured.format != video::CapturedFrame::Format::BGRA || captured.width <= 0 || captured.height <= 0 ||
                captured.stride < captured.width*4 || captured.data.size() < static_cast<size_t>(captured.stride)*captured.height) return;
            QImage image(captured.data.data(), captured.width, captured.height, captured.stride, QImage::Format_ARGB32);
            std::lock_guard lock(item->imageMutex); item->image = image.copy();
        };
        auto audioChunk = [weak](audio::StreamChunk &&chunk) {
            const auto item = weak.lock(); if (!item) return;
            std::lock_guard lock(item->audioMutex);
            auto normalized = audio::normalizeAudioForOpus(chunk, &item->resampler);
            item->samples.insert(item->samples.end(), normalized.begin(), normalized.end());
            // Keep at most 200 ms. A stalled UI must not accumulate delayed audio.
            while (item->samples.size() > 19200) item->samples.pop_front();
        };
        auto audioResult = [&](const audio::CaptureResult &result) {
            if (!result.success) item->error = "Audio unavailable: " + QString::fromStdString(result.error);
        };
        if (item->kind == "window") {
            item->window = std::make_unique<video::WindowCapture>(); item->window->setFrameCallback(frame);
            if (!item->window->startCapture(item->locator.toStdString(), Width, Height, 30, true)) item->error = "Window unavailable. Remove it and add the window again.";
            else {
                for (const auto &info : item->window->getWindows()) if (QString::fromStdString(info.id) == item->locator) {
                    item->audio = std::make_unique<audio::WindowAudioCaptureCore>();
                    audioResult(item->audio->StartStreamCapture(info.processId, audioChunk)); break;
                }
            }
        } else if (item->kind == "camera") {
            item->camera = std::make_unique<video::CameraCapture>(); item->camera->setFrameCallback(frame);
            if (!item->camera->startCapture(item->locator.toStdString(), Width, Height, 30)) item->error = QString::fromStdString(item->camera->lastError());
        } else if (item->kind == "spout") {
            item->spout = std::make_unique<video::SpoutCapture>(); item->spout->setFrameCallback(frame);
            if (!item->spout->startCapture(item->locator.toStdString(), Width, Height, 30)) item->error = "Spout sender unavailable. Start the sender and add it again.";
        } else if (item->kind == "microphone" || item->kind == "desktop") {
            item->audio = std::make_unique<audio::WindowAudioCaptureCore>();
            if (item->kind == "desktop") audioResult(item->audio->StartDefaultEndpointStreamCapture(audio::DefaultAudioEndpoint::MultimediaOutput, audioChunk));
            else audioResult(item->audio->StartInputDeviceStreamCapture(item->locator.toStdString(), audioChunk));
        } else if (item->kind == "browser" || item->kind == "guest" || item->kind == "wireless" || item->kind == "chat") {
            if (!webUrl(item->locator)) { item->error = "Invalid browser URL."; return; }
            item->browser = new BrowserSource(item->id, QUrl(item->locator));
            QObject::connect(item->browser, &BrowserSource::frameReady, q, [weak](const QImage &image) {
                if (auto item = weak.lock()) { std::lock_guard lock(item->imageMutex); item->image = image; }
            });
            QObject::connect(item->browser, &BrowserSource::errorOccurred, q, [this, weak](const QString &message) {
                if (auto item = weak.lock()) { item->error = message; this->message(item->name+": "+message); }
            });
            QObject::connect(item->browser, &BrowserSource::audioProcessReady, q, [this, weak, audioChunk](quint32 pid) {
                if (auto item = weak.lock(); item && item->hasAudio()) {
                    item->audio = std::make_unique<audio::WindowAudioCaptureCore>();
                    const auto result = item->audio->StartStreamCapture(pid, audioChunk);
                    if (!result.success) { item->error = "Browser audio unavailable: " + QString::fromStdString(result.error); message(item->error); }
                }
            });
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

    void removeSource() {
        auto item = source(selectedSource); if (!item) return;
        const bool usedOnProgram = std::any_of(program.layers.begin(), program.layers.end(), [&](const auto &layer) { return layer.source == item->id && layer.visible; });
        if (usedOnProgram || (fading && std::any_of(previous.layers.begin(), previous.layers.end(), [&](const auto &layer) { return layer.source == item->id && layer.visible; }))) {
            message("Take a scene without this source before removing it from the show."); return;
        }
        for (auto &scene : scenes) std::erase_if(scene.layers, [&](const auto &layer) { return layer.source == item->id; });
        { std::lock_guard lock(sourcesMutex); std::erase(sources, item); }
        // Destroy capture owners on the UI thread, including WebView2's apartment.
        item->onProgram = false; item->muted = true;
        if (item->window) item->window->stopCapture();
        if (item->camera) item->camera->stopCapture();
        if (item->spout) item->spout->stopCapture();
        if (item->audio) item->audio->StopCapture();
        delete item->browser.data();
        selectedSource.clear(); rebuild(); changed();
    }

    void addInvite(bool cameraOnly) {
        bool ok; const auto name = QInputDialog::getText(q, cameraOnly ? "Wireless camera" : "Invite guest", "Source name", QLineEdit::Normal,
            cameraOnly ? "Wireless camera" : "Guest", &ok);
        if (!ok || name.trimmed().isEmpty()) return;
        auto item = std::make_shared<Source>(); item->kind = cameraOnly ? "wireless" : "guest"; item->name = name.trimmed();
        const auto stream = "mix" + newId().remove('-'); const auto password = newId().remove('-');
        QUrl view("https://vdo.ninja/"); QUrlQuery viewQuery;
        viewQuery.addQueryItem("view", stream); viewQuery.addQueryItem("password", password);
        viewQuery.addQueryItem("solo", ""); viewQuery.addQueryItem("cleanoutput", ""); view.setQuery(viewQuery); item->locator = view.toString(QUrl::FullyEncoded);
        QUrl push("https://vdo.ninja/"); QUrlQuery pushQuery;
        pushQuery.addQueryItem("push", stream); pushQuery.addQueryItem("password", password); pushQuery.addQueryItem("label", item->name);
        if (cameraOnly) pushQuery.addQueryItem("rear", "");
        push.setQuery(pushQuery); item->invite = push.toString(QUrl::FullyEncoded);
        appendSource(item); showInvite(item);
    }
    void showInvite(const std::shared_ptr<Source> &item) {
        QDialog dialog(q); dialog.setWindowTitle(item->name+" - invite"); auto *layout = new QVBoxLayout(&dialog);
        auto *help = new QLabel("Send this private link to the guest or open it on your phone.\nThey choose their camera and microphone, then join.\nAdd the source to your scene and press Take when ready.", &dialog); layout->addWidget(help);
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
        if (webUrl(value)) item->locator = value;
        else {
            if (value.contains('/') || value.size() > 128) { message("Enter a session ID or a complete http(s) overlay URL."); return; }
            QUrl url("https://socialstream.ninja/featured.html"); QUrlQuery query; query.addQueryItem("session", value); url.setQuery(query);
            item->locator = url.toString(QUrl::FullyEncoded);
        }
        appendSource(item);
        message("Chat overlay added. Select it and choose Open / Invite to open the chat dock and feature messages; then Take this scene to Program.");
    }

    void applyLayout(const QString &name) {
        std::vector<Layer*> layers;
        for (auto &layer : preview().layers) {
            auto item = source(layer.source);
            if (layer.visible && item && item->kind != "microphone" && item->kind != "desktop") layers.push_back(&layer);
        }
        if (layers.empty()) return;
        if (name == "Full") { if (auto *layer = selectedLayer()) layer->rect = {0, 0, 1, 1}; }
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
        for (int i = 0; i < static_cast<int>(layers.size()); ++i) if (layers[i].source == selectedSource) {
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
            [&](const auto &layer) { return layer.source == item->id && layer.visible; });
    }
    QImage compose(const Scene &scene) {
        QImage image(Width, Height, QImage::Format_ARGB32_Premultiplied); image.fill(scene.background);
        QPainter painter(&image); painter.setRenderHint(QPainter::SmoothPixmapTransform);
        for (const auto &layer : scene.layers) {
            if (!layer.visible) continue;
            auto item = source(layer.source); if (!item || item->kind == "microphone" || item->kind == "desktop") continue;
            QImage frame; { std::lock_guard lock(item->imageMutex); frame = item->image; }
            if (frame.isNull()) continue;
            const QRectF bounds(layer.rect.x()*Width, layer.rect.y()*Height, layer.rect.width()*Width, layer.rect.height()*Height);
            QSizeF fitted(frame.size()); fitted.scale(bounds.size(), Qt::KeepAspectRatio);
            const QRectF target(bounds.center().x()-fitted.width()/2, bounds.center().y()-fitted.height()/2, fitted.width(), fitted.height());
            painter.setOpacity(layer.opacity); painter.drawImage(target, frame);
        }
        return image;
    }
    void render() {
        previewView->image = compose(preview());
        previewView->selection = selectedLayer() ? selectedLayer()->rect : QRectF(); previewView->update();
        QImage frame = compose(program);
        if (fading) {
            const double fraction = std::min(1.0, transition.elapsed()/400.0);
            QImage old = compose(previous); QPainter painter(&old); painter.setOpacity(fraction); painter.drawImage(0, 0, frame); painter.end(); frame = old;
            if (fraction >= 1) fading = false;
        }
        programView->image = frame; programView->update();
        if (outputView) { outputView->image = frame; outputView->update(); }
        previewTitle->setText("PREVIEW  /  " + preview().name);
        const bool publishingProgram = core->isLive() && core->getSourceHealth().mode == app::VideoSourceMode::Mixer;
        programTitle->setText(QString(publishingProgram ? "LIVE OUTPUT  /  " : "PROGRAM  /  ") + program.name);
        if (core->isLive()) {
            video::CapturedFrame captured;
            captured.width = Width; captured.height = Height; captured.stride = frame.bytesPerLine(); captured.timestamp = now100ns();
            captured.data.assign(frame.constBits(), frame.constBits()+frame.sizeInBytes()); core->submitMixerVideo(std::move(captured));
        }
        for (const auto &[weak, meter] : meters) if (auto item = weak.lock()) meter->setValue(qRound(item->peak*100));
        masterMeter->setValue(qRound(masterPeak*100));
    }
    void mixAudio() {
        auto next = std::chrono::steady_clock::now();
        while (running) {
            next += std::chrono::milliseconds(10);
            audio::StreamChunk chunk; chunk.samples.assign(960, 0); chunk.captureTime100ns = now100ns();
            {
            std::lock_guard sourcesLock(sourcesMutex);
            for (const auto &item : sources) {
                float peak = 0; const float gain = (!item->muted && (!item->followsScene || item->onProgram)) ? item->gain.load() : 0;
                std::lock_guard lock(item->audioMutex);
                const auto count = std::min(item->samples.size(), chunk.samples.size());
                for (size_t i = 0; i < count; ++i) {
                    const float value = item->samples.front()*gain; item->samples.pop_front();
                    chunk.samples[i] += value; peak = std::max(peak, std::abs(value));
                }
                item->peak = std::max(peak, item->peak.load()*0.92f);
            }
            }
            float peak = 0;
            for (auto &value : chunk.samples) { value *= masterGain; peak = std::max(peak, std::abs(value)); }
            if (peak > 0.98f) for (auto &value : chunk.samples) value *= 0.98f/peak;
            masterPeak = std::min(0.98f, peak);
            if (core->isLive()) core->submitMixerAudio(std::move(chunk));
            if (next < std::chrono::steady_clock::now()-std::chrono::milliseconds(100)) next = std::chrono::steady_clock::now();
            std::this_thread::sleep_until(next);
        }
    }

    bool save(const QString &path) {
        QJsonArray jsonSources, jsonScenes;
        for (const auto &item : sources) jsonSources.append(QJsonObject{{"id", item->id}, {"name", item->name}, {"kind", item->kind},
            {"locator", item->locator}, {"invite", item->invite}, {"color", item->color.name()}, {"gain", item->gain.load()},
            {"muted", item->muted.load()}, {"followsScene", item->followsScene.load()}});
        for (const auto &scene : scenes) {
            QJsonArray layers;
            for (const auto &layer : scene.layers) layers.append(QJsonObject{{"source", layer.source}, {"x", layer.rect.x()}, {"y", layer.rect.y()},
                {"w", layer.rect.width()}, {"h", layer.rect.height()}, {"visible", layer.visible}, {"opacity", layer.opacity}});
            jsonScenes.append(QJsonObject{{"id", scene.id}, {"name", scene.name}, {"background", scene.background.name()}, {"layers", layers}});
        }
        QDir().mkpath(QFileInfo(path).absolutePath()); QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly)) { message("Could not save show: " + file.errorString()); return false; }
        const auto bytes = QJsonDocument(QJsonObject{{"version", 1}, {"sources", jsonSources}, {"scenes", jsonScenes}}).toJson();
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
        const QStringList kinds{"window", "camera", "microphone", "desktop", "spout", "browser", "guest", "wireless", "chat", "image", "title", "color"};
        for (const auto &value : sourceData) {
            const auto obj = value.toObject(); auto item = std::make_shared<Source>();
            item->id = obj["id"].toString(); item->name = obj["name"].toString().left(256); item->kind = obj["kind"].toString();
            if (QUuid(item->id).isNull() || sourceIds.contains(item->id) || !kinds.contains(item->kind)) { message("Invalid or duplicate source in show."); return false; }
            sourceIds.insert(item->id);
            item->locator = obj["locator"].toString().left(16384); item->invite = obj["invite"].toString().left(16384);
            if (!item->invite.isEmpty() && !webUrl(item->invite)) { message("Invalid invite URL in show."); return false; }
            item->color = QColor(obj["color"].toString()); if (!item->color.isValid()) item->color = QColor("#183657");
            item->gain = static_cast<float>(std::clamp(obj["gain"].toDouble(1), 0.0, 2.0)); item->muted = obj["muted"].toBool(); item->followsScene = obj["followsScene"].toBool(true);
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
                    std::clamp(layer["w"].toDouble(1), 0.01, 1.0), std::clamp(layer["h"].toDouble(1), 0.01, 1.0)}, layer["visible"].toBool(true), std::clamp(layer["opacity"].toDouble(1), 0.0, 1.0)});
            }
            incomingScenes.push_back(scene);
        }
        loading = true;
        std::vector<std::shared_ptr<Source>> old;
        { std::lock_guard lock(sourcesMutex); old.swap(sources); sources = std::move(incoming); }
        for (auto &item : old) {
            if (item->window) item->window->stopCapture(); if (item->camera) item->camera->stopCapture();
            if (item->spout) item->spout->stopCapture(); if (item->audio) item->audio->StopCapture(); delete item->browser.data();
        }
        scenes = std::move(incomingScenes);
        for (auto &item : sources) {
            // HWNDs are transient and may belong to a different process after a restart.
            if (item->kind == "window") item->error = "Re-add this window after reopening a show; window handles are not persistent.";
            else startSource(item);
        }
        loading = false; return true;
    }
};

MixerWidget::MixerWidget(app::VersusApp *core, QWidget *parent) : QWidget(parent), impl_(std::make_unique<Impl>(this, core)) {}
MixerWidget::~MixerWidget() = default;
} // namespace versus::mixer
