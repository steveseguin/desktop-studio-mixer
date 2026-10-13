#pragma once
#include <QImage>
#include <QString>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <memory>
#include <thread>
namespace versus::mixer {
class LocalOutputs {
public:
    LocalOutputs();
    ~LocalOutputs();
    void configure(bool spout, bool camera, int fps);
    void submit(const QImage &frame, int64_t timestamp);
    QString status() const;
    static QString registerCamera(bool install);
    uint64_t completedFrames() const { return completedFrames_; }
private:
    struct IsolatedOutput;
    std::unique_ptr<IsolatedOutput> isolatedSpout_, isolatedCamera_;
    std::atomic<uint64_t> completedFrames_{0};
    void run();
    std::atomic<bool> running_{false}, spout_{false}, camera_{false};
    std::atomic<int> fps_{30};
    mutable std::mutex mutex_;
    std::condition_variable wake_;
    std::thread worker_;
    QImage pending_;
    int64_t timestamp_ = 0;
    QString status_;
};
}
