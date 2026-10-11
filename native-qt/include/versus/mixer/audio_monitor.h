#pragma once
#include <QString>
#include <vector>
#include <memory>

namespace versus::mixer {
// All instance methods run on the audio worker. Device enumeration is independent.
class AudioMonitor {
public:
    AudioMonitor();
    ~AudioMonitor();
    static std::vector<std::pair<int, QString>> devices();
    QString open(int device);
    void close();
    void write(const std::vector<float> &samples);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
// GUI-thread registration. One shared scan silences all owned WebView2 trees;
// reference counts allow controllers sharing a browser process. Browser samples
// arrive through the Web Audio bridge, independently of Windows session volume.
void registerBrowserPlayback(unsigned long browserPid);
void unregisterBrowserPlayback(unsigned long browserPid);
}
