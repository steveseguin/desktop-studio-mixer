#pragma once
#include <QCoreApplication>
#include <QImage>
#include <QString>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <mutex>

namespace versus::mixer::detail {
// Opt-in synthetic-media diagnostics. No image pixels, URLs or audio are saved.
// Each process has its own bounded file; disabled runs do no image sampling.
class ImageTrace {
public:
    static ImageTrace &instance() { static ImageTrace trace; return trace; }
    void record(const char *stage, const QImage &image) {
        if (!enabled_ || image.isNull()) return;
        std::lock_guard lock(mutex_);
        if (rows_ >= 100000) return;
        if (image.depth() != 32) return;
        uint64_t hash = 14695981039346656037ull;
        for (int y=0; y<36; ++y) {
            const auto *row = reinterpret_cast<const uint32_t*>(image.constScanLine(y*image.height()/36));
            for (int x=0; x<64; ++x) {
                hash ^= row[x*image.width()/64] & 0xffffff;
                hash *= 1099511628211ull;
            }
        }
        const auto now = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        file_ << stage << ',' << now << ',' << hash << '\n';
        if (++rows_%300 == 0) file_.flush();
    }
private:
    ImageTrace() {
        const auto path = qEnvironmentVariable("VIDEO_MIXER_IMAGE_TRACE");
        if (path.isEmpty()) return;
        file_.open(std::filesystem::path((path+"-"+QString::number(QCoreApplication::applicationPid())+".csv").toStdWString()));
        enabled_ = file_.is_open();
        if (enabled_) file_ << "stage,steadyUs,imageHash\n";
    }
    bool enabled_ = false;
    unsigned rows_ = 0;
    std::ofstream file_;
    std::mutex mutex_;
};
}
