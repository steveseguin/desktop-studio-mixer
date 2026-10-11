// A separate consumer for testing the packaged app's real Spout output.
#include <SpoutLibrary.h>
#include <chrono>
#include <cstdio>
#include <set>
#include <thread>
#include <vector>
int main() {
    auto *receiver = GetSpout();
    if (!receiver || !receiver->CreateOpenGL(nullptr)) return 2;
    receiver->SetReceiverName("Desktop Studio Program");
    std::vector<unsigned char> pixels(1280*720*4);
    std::set<unsigned long long> hashes;
    unsigned width = 1280, height = 720, frames = 0;
    const auto started = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now()-started < std::chrono::seconds(15) && frames < 60) {
        if (receiver->ReceiveImage(pixels.data(), GL_BGRA)) {
            if (receiver->IsUpdated()) {
                width = receiver->GetSenderWidth(); height = receiver->GetSenderHeight();
                if (width > 4096 || height > 4096) return 3;
                pixels.resize(size_t(width)*height*4);
            } else if (receiver->IsFrameNew()) {
                unsigned long long hash = 1469598103934665603ULL;
                for (size_t i = 0; i < pixels.size(); i += 29) { hash ^= pixels[i]; hash *= 1099511628211ULL; }
                hashes.insert(hash); ++frames;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    std::printf("{\"frames\":%u,\"distinctFrames\":%zu,\"width\":%u,\"height\":%u}\n",frames,hashes.size(),width,height);
    receiver->ReleaseReceiver(); receiver->CloseOpenGL(); receiver->Release();
    return frames >= 45 && hashes.size() >= 20 ? 0 : 1;
}
