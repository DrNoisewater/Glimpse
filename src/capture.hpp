#pragma once
#include "policy.hpp"
#include "audio_policy.hpp"
#include <windows.h>
#include <atomic>
#include <condition_variable>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>

namespace glimpse {
constexpr UINT WM_CAPTURE_STARTED = WM_APP + 10;
constexpr UINT WM_CAPTURE_FINISHED = WM_APP + 11;
// lParam: heap std::wstring* the receiver deletes. An audio source dropped out or came back.
constexpr UINT WM_CAPTURE_NOTICE = WM_APP + 12;
struct CaptureResult {
    std::filesystem::path file, partial;
    std::wstring error;
    double seconds{}, startMs{};
    uint64_t frames{}, lateFrames{}, bytes{};
    unsigned audioInterrupted{}, audioNeverCaptured{}; // AudioMic/AudioSystem bits
    bool cancelled{}, gpuInput{}, qualityVbr{}, highProfile{};
};
class Recorder {
    std::thread worker_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::atomic<bool> stop_{}, cancel_{}, pause_{};
    std::atomic<int64_t> elapsed_{};
    std::atomic<unsigned> audioMode_{};
    std::atomic<float> micGain_{1.f}, systemGain_{.65f};
    std::atomic<float> micLevel_{},systemLevel_{};
    void run(HWND owner, Rect area, VideoSpec spec, std::filesystem::path folder, bool forceSoftware, unsigned audioMode, bool diagnostics) noexcept;
public:
    ~Recorder();
    void start(HWND owner, Rect area, VideoSpec spec, const std::filesystem::path& folder, bool forceSoftware, unsigned audioMode, bool diagnostics = false);
    void stop(bool discard = false);
    void pause(bool value);
    void audio(unsigned mode) { audioMode_=mode; }
    void volume(float mic,float system) { micGain_=mic; systemGain_=system; }
    float micLevel() const { return micLevel_.load(); }
    float systemLevel() const { return systemLevel_.load(); }
    double elapsed() const { return double(elapsed_.load())/10000000.; }
    void join();
};
}
