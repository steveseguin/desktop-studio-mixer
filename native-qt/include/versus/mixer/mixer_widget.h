#pragma once
#include <QWidget>
#include <memory>
namespace versus::app { class VersusApp; }
namespace versus::mixer {
class MixerWidget : public QWidget {
    Q_OBJECT
public:
    explicit MixerWidget(app::VersusApp *core, QWidget *parent = nullptr);
    ~MixerWidget() override;
    void setOutputState(const QString &label, const QString &status, bool enabled, bool active);
    void shutdown();
signals:
    void outputRequested();
    void toggleOutputRequested();
    void recordRequested();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace versus::mixer
