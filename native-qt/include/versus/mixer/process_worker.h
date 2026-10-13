#pragma once
#include <QByteArray>
#include <QJsonObject>
#include <QImage>
#include <memory>
#include <mutex>
#include "versus/audio/window_audio_capture_core.h"
#include "versus/video/window_capture.h"

namespace versus::mixer {
// Anonymous, explicitly inherited handles only. No listening port, named pipe,
// shared file, command-line credentials, or script-accessible host object.
// Each worker owns a kill-on-close job, including any encoder/browser children.
class ProcessWorker {
public:
    ProcessWorker();
    ~ProcessWorker();
    bool start(const QString &kind);
    bool alive() const;
    void terminate();
    QJsonObject stop();
    QJsonObject call(const QJsonObject &command, const void *data = nullptr,
                     size_t size = 0, QByteArray *reply = nullptr, int timeoutMs = 3000);
    bool frame(video::CapturedFrame &frame);
    QImage image(bool premultiplied = false);
    bool audio(audio::StreamChunk &chunk);
    void waitForMedia(int timeoutMs);
    QJsonObject status() const;
    QString error() const;
    static bool inWorker();
    static QJsonObject browserBudgetState();
    static int run(int argc, char **argv);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    mutable std::mutex calls_;
};
} // namespace versus::mixer
