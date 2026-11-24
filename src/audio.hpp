#pragma once
#include "audio_policy.hpp"
#include "policy.hpp"
#include <windows.h>
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <winrt/base.h>
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace glimpse {
int64_t qpcTicks();

// Audio capture workers are independent of the potentially blocking screen
// compositor / H.264 encoder thread. Only that encoder thread writes to the
// Media Foundation sink; a mutex protects the timestamped PCM staging ring.
class AudioRecorder {
    struct Endpoint;
    friend struct Endpoint;
    struct Mapping {
        int64_t anchorQpc{};
        int64_t anchorTime{};
        uint64_t epoch{};
        bool active{};
    };
    std::unique_ptr<Endpoint> mic_,system_;
    // A source that cannot be opened (e.g. another app holds the microphone in
    // exclusive mode) never stops the recording: it is recorded as silence,
    // retried every couple of seconds, and reported through takeNotices().
    AudioSourceHealth micHealth_,systemHealth_;
    std::vector<std::wstring> notices_;
    void keep(std::unique_ptr<Endpoint>& endpoint,AudioSourceHealth& health,bool system,bool wanted);
    mutable std::mutex ringMutex_, mappingMutex_;
    AudioRing ring_{240000}; // Five seconds of PCM to survive occasional encoder stalls.
    Mapping mapping_;
    DWORD streamIndex_{};
    bool enabled_{};
    std::atomic<uint64_t> discontinuities_{}, badTimestamps_{}, timestampCorrections_{}, inferredTimestamps_{};
    float micLevel_{},systemLevel_{};
    std::atomic<float> micGain_{1.f},systemGain_{.65f};
public:
    AudioRecorder();
    ~AudioRecorder();
    void configure(IMFSinkWriter* writer,bool enabled);
    void collect(unsigned mode,const Timeline* timeline,int64_t activeFrom,bool paused,float micGain=1.f,float systemGain=.65f);
    void writeUntil(IMFSinkWriter* writer,int64_t endTime);
    void close();
    uint64_t dropped() const { std::lock_guard lock(ringMutex_); return ring_.dropped; }
    uint64_t glitches() const { return discontinuities_.load(); }
    uint64_t badTimestamps() const { return badTimestamps_.load(); }
    uint64_t timestampCorrections() const { return timestampCorrections_.load(); }
    uint64_t inferredTimestamps() const { return inferredTimestamps_.load(); }
    uint64_t overloadedFrames() const { std::lock_guard lock(ringMutex_); return ring_.overloadedFrames; }
    float mixedPeak() const { std::lock_guard lock(ringMutex_); return ring_.peakBeforeLimiter; }
    // Messages for the user about sources that dropped out or came back.
    std::vector<std::wstring> takeNotices() { return std::exchange(notices_,{}); }
    // AudioMic/AudioSystem bits for sources that were missing at some point.
    unsigned interrupted() const { return (micHealth_.everDown?AudioMic:0u)|(systemHealth_.everDown?AudioSystem:0u); }
    // ...and for sources that never delivered audio at all.
    unsigned neverCaptured() const { return (micHealth_.everDown&&!micHealth_.everUp?AudioMic:0u)|(systemHealth_.everDown&&!systemHealth_.everUp?AudioSystem:0u); }
    float micLevel() const { return micLevel_; }
    float systemLevel() const { return systemLevel_; }
};
}
