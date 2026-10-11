#include "versus/mixer/media_source.h"
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl.h>
#include <propvarutil.h>
#include <algorithm>
#include <cstring>
#include <set>
using Microsoft::WRL::ComPtr;
namespace versus::mixer {
MediaSource::~MediaSource() { stop(); }
void MediaSource::stop() { running_ = false; wake_.notify_all(); if (worker_.joinable()) worker_.join(); }
void MediaSource::start(const QString &path, std::function<void(video::CapturedFrame)> frame,
                        std::function<void(audio::StreamChunk&&)> audio) {
    stop(); running_ = true; ended_ = false; hasVideo_ = false; seek_ = false;
    { std::lock_guard lock(mutex_); error_.clear(); }
    worker_ = std::thread([this, path, frame, audio] {
        const HRESULT apartment = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        const HRESULT mf = MFStartup(MF_VERSION, MFSTARTUP_LITE);
        auto fail = [this](const QString &error) { std::lock_guard lock(mutex_); error_ = error; };
        {
            ComPtr<IMFAttributes> attributes;
            HRESULT hr = FAILED(mf) ? mf : MFCreateAttributes(&attributes, 2);
            if (SUCCEEDED(hr)) hr = attributes->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);
            ComPtr<IMFSourceReader> reader;
            if (SUCCEEDED(hr)) hr = MFCreateSourceReaderFromURL(path.toStdWString().c_str(), attributes.Get(), &reader);
            if (FAILED(hr)) fail("Cannot open this media file. Use a Windows-supported format such as H.264/AAC MP4.");
            else {
                reader->SetStreamSelection(MF_SOURCE_READER_ALL_STREAMS, FALSE);
                DWORD videoStream = DWORD(-1), audioStream = DWORD(-1); UINT32 width = 0, height = 0;
                LONG stride = 0;
                for (DWORD index = 0; index < 32; ++index) {
                    ComPtr<IMFMediaType> native;
                    if (FAILED(reader->GetNativeMediaType(index, 0, &native))) break;
                    GUID major{}; native->GetGUID(MF_MT_MAJOR_TYPE, &major);
                    const bool isVideo = major == MFMediaType_Video && videoStream == DWORD(-1);
                    const bool isAudio = major == MFMediaType_Audio && audioStream == DWORD(-1);
                    if (!isVideo && !isAudio) continue;
                    ComPtr<IMFMediaType> type; MFCreateMediaType(&type); type->SetGUID(MF_MT_MAJOR_TYPE, major);
                    type->SetGUID(MF_MT_SUBTYPE, isVideo ? MFVideoFormat_RGB32 : MFAudioFormat_Float);
                    if (isAudio) {
                        type->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, 2); type->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, 48000);
                        type->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 32); type->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, 8);
                        type->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, 384000);
                    }
                    if (FAILED(reader->SetCurrentMediaType(index, nullptr, type.Get()))) continue;
                    reader->SetStreamSelection(index, TRUE);
                    if (isVideo) {
                        videoStream = index; reader->GetCurrentMediaType(index, &type);
                        MFGetAttributeSize(type.Get(), MF_MT_FRAME_SIZE, &width, &height);
                        stride = LONG(MFGetAttributeUINT32(type.Get(), MF_MT_DEFAULT_STRIDE, width*4));
                    } else audioStream = index;
                }
                if (videoStream == DWORD(-1) && audioStream == DWORD(-1)) fail("No supported video or audio track in this file.");
                else if (width > 8192 || height > 8192) fail("Media dimensions exceed 8192 pixels.");
                else {
                    hasVideo_ = videoStream != DWORD(-1);
                    using Clock = std::chrono::steady_clock;
                    auto origin = Clock::now(); std::set<DWORD> finished;
                    while (running_) {
                        if (paused_) {
                            const auto pauseStart = Clock::now(); std::unique_lock lock(mutex_);
                            wake_.wait(lock, [this] { return !running_ || !paused_ || seek_; });
                            origin += Clock::now()-pauseStart; if (!running_) break;
                        }
                        if (seek_.exchange(false)) {
                            PROPVARIANT position{}; InitPropVariantFromInt64(0, &position);
                            if (videoStream != DWORD(-1)) reader->SetStreamSelection(videoStream, TRUE);
                            if (audioStream != DWORD(-1)) reader->SetStreamSelection(audioStream, TRUE);
                            hr = reader->SetCurrentPosition(GUID_NULL, position); PropVariantClear(&position);
                            if (FAILED(hr)) { fail("This file cannot seek or loop."); break; }
                            origin = Clock::now(); finished.clear(); ended_ = false;
                        }
                        DWORD stream = 0, flags = 0; LONGLONG pts = 0; ComPtr<IMFSample> sample;
                        hr = reader->ReadSample(MF_SOURCE_READER_ANY_STREAM, 0, &stream, &flags, &pts, &sample);
                        if (FAILED(hr)) { fail("Media decoding failed."); break; }
                        if ((flags & MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED) && stream == videoStream) {
                            ComPtr<IMFMediaType> type;
                            if (FAILED(reader->GetCurrentMediaType(stream, &type)) || FAILED(MFGetAttributeSize(type.Get(), MF_MT_FRAME_SIZE, &width, &height)) || !width || !height || width > 8192 || height > 8192) {
                                fail("Unsupported change in video format."); break;
                            }
                            stride = LONG(MFGetAttributeUINT32(type.Get(), MF_MT_DEFAULT_STRIDE, width*4));
                        }
                        if (flags & MF_SOURCE_READERF_ENDOFSTREAM) {
                            finished.insert(stream); reader->SetStreamSelection(stream, FALSE);
                            if ((videoStream == DWORD(-1) || finished.contains(videoStream)) && (audioStream == DWORD(-1) || finished.contains(audioStream))) {
                                if (loop_) seek_ = true;
                                else { ended_ = true; paused_ = true; }
                            }
                            continue;
                        }
                        if (!sample) continue;
                        const auto deadline = origin + std::chrono::nanoseconds(std::max<LONGLONG>(0, pts)*100);
                        { std::unique_lock lock(mutex_); wake_.wait_until(lock, deadline, [this] { return !running_ || paused_ || seek_; }); }
                        if (!running_) break;
                        if (seek_) continue;
                        // Preserve this sample while paused; do not consume media off the clock.
                        if (paused_) {
                            const auto pauseStart = Clock::now(); std::unique_lock lock(mutex_);
                            wake_.wait(lock, [this] { return !running_ || !paused_ || seek_; });
                            origin += Clock::now()-pauseStart;
                            if (!running_) break; if (seek_) continue;
                        }
                        const auto timestamp = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch()).count()/100;
                        ComPtr<IMFMediaBuffer> buffer; if (FAILED(sample->ConvertToContiguousBuffer(&buffer))) continue;
                        BYTE *bytes = nullptr; DWORD length = 0;
                        if (FAILED(buffer->Lock(&bytes, nullptr, &length))) continue;
                        if (stream == videoStream && width && height && length >= size_t(std::abs(stride))*height) {
                            video::CapturedFrame captured; captured.width = width; captured.height = height; captured.stride = width*4;
                            captured.timestamp = timestamp; captured.opaque = true; captured.data.resize(size_t(width)*height*4);
                            for (UINT32 row = 0; row < height; ++row) {
                                auto *out = captured.data.data()+size_t(row)*width*4;
                                const auto *in = bytes+size_t(stride < 0 ? height-1-row : row)*std::abs(stride);
                                std::memcpy(out, in, width*4);
                                for (UINT32 col = 0; col < width; ++col) out[col*4+3] = 255;
                            }
                            frame(std::move(captured));
                        } else if (stream == audioStream && length%8 == 0) {
                            audio::StreamChunk chunk; chunk.sampleRate = 48000; chunk.channels = 2; chunk.captureTime100ns = timestamp;
                            chunk.samples.resize(length/sizeof(float)); std::memcpy(chunk.samples.data(), bytes, length); audio(std::move(chunk));
                        }
                        buffer->Unlock();
                    }
                }
            }
        }
        running_ = false;
        if (SUCCEEDED(mf)) MFShutdown();
        if (SUCCEEDED(apartment)) CoUninitialize();
    });
}
}
