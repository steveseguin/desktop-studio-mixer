#pragma once
#include <QWidget>
#include <QJsonObject>
#include <QImage>
#include <memory>
namespace versus::app { class VersusApp; }
namespace versus::mixer {
class MixerWidget : public QWidget {
    Q_OBJECT
public:
    explicit MixerWidget(app::VersusApp *core, QWidget *parent = nullptr);
    ~MixerWidget() override;
    // Let the scroll host shrink flexible canvases/lists to their minimum sizes.
    // QLabel wrapping otherwise makes it request the entire preferred height.
    bool hasHeightForWidth() const override { return false; }
    int heightForWidth(int) const override { return -1; }
    void setOutputState(const QString &label, const QString &status, bool enabled, bool active);
    void shutdown();
    QSize canvasSize() const;
    int canvasFrameRate() const;
    QJsonObject controlState() const;
    QJsonObject controlCommand(const QJsonObject &command);
    QImage captureCanvas(const QString &target);
    void setWorkspaceLayout(const QString &layout);
signals:
    void outputRequested();
    void toggleOutputRequested();
    void recordRequested();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace versus::mixer
