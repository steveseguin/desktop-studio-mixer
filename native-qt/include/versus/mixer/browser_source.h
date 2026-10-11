#pragma once

#include <QImage>
#include <QWidget>
#include <memory>

namespace versus::mixer {

// Owns an Evergreen WebView2 instance. Only the SDK loader ships with the app.
class BrowserSource : public QWidget {
    Q_OBJECT
public:
    BrowserSource(const QString &id, const QUrl &url, bool renderFrames = true,
                  bool transparent = false, bool allowTalkback = false);
    ~BrowserSource() override;
    void interact();
    void park();
    QString captureDescription() const;
signals:
    void frameReady(const QImage &image);
    void audioProcessReady(quint32 processId);
    void audioReady(const QByteArray &pcm, int sampleRate);
    void errorOccurred(const QString &message);
protected:
    void resizeEvent(QResizeEvent *event) override;
    void closeEvent(QCloseEvent *event) override;
private:
    void capture();
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace versus::mixer
