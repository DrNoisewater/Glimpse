#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>
namespace glimpse {
enum AudioMode : unsigned { AudioSilent=0, AudioMic=1, AudioSystem=2, AudioBoth=3 };
// Why a microphone or system-audio endpoint could not be opened or stopped
// delivering. Values are the WASAPI / Win32 HRESULTs, kept portable for tests.
enum class AudioProblem { InUse, Blocked, Missing, Lost, Other };
inline AudioProblem classifyAudioFailure(uint32_t hr) {
    switch(hr) {
    case 0x8889000Au: // AUDCLNT_E_DEVICE_IN_USE: another app holds it in exclusive mode
        return AudioProblem::InUse;
    case 0x80070005u: // E_ACCESSDENIED: Windows microphone privacy setting
        return AudioProblem::Blocked;
    case 0x80070490u: // E_NOTFOUND: no default endpoint
        return AudioProblem::Missing;
    case 0x88890004u: // AUDCLNT_E_DEVICE_INVALIDATED: unplugged, or taken over mid-recording
    case 0x88890026u: // AUDCLNT_E_RESOURCES_INVALIDATED
    case 0x88890010u: // AUDCLNT_E_SERVICE_NOT_RUNNING
        return AudioProblem::Lost;
    default: return AudioProblem::Other;
    }
}
// Short, actionable tray text. Recording always continues; the source is
// recorded as silence and retried until it becomes available again.
inline std::wstring audioProblemText(bool system,uint32_t hr) {
    const wchar_t* source=system?L"System audio":L"Microphone";
    const wchar_t* lower=system?L"system audio":L"the microphone";
    const std::wstring tail=L" Recording continues without it and picks it up again when it's free.";
    switch(classifyAudioFailure(hr)) {
    case AudioProblem::InUse:
        return std::wstring(L"Another app has exclusive control of ")+(system?L"the sound output":L"the microphone")+
            L", so Windows won't share it."+tail;
    case AudioProblem::Blocked:
        return std::wstring(L"Windows privacy settings are blocking ")+lower+L" for desktop apps."+tail;
    case AudioProblem::Missing:
        return std::wstring(L"No default ")+(system?L"sound output":L"microphone")+L" was found."+tail;
    case AudioProblem::Lost:
        return std::wstring(source)+L" was disconnected or taken over by another app."+tail;
    default: {
        wchar_t code[16]{};
        static const wchar_t hex[]=L"0123456789ABCDEF";
        for(int i=0;i<8;++i) code[i]=hex[(hr>>(28-4*i))&15];
        return std::wstring(L"Windows couldn't start ")+lower+L" (error 0x"+code+L")."+tail;
    }
    }
}
// Retry pacing for an unavailable endpoint, in 100 ns ticks. Also records
// whether the source was ever missing so the saved-file summary can say so.
struct AudioSourceHealth {
    static constexpr int64_t retryInterval=20000000; // 2 s
    int64_t retryAt{};
    bool down{}, everDown{}, everUp{};
    bool shouldTry(int64_t now) const { return !down || now>=retryAt; }
    // Returns true when this is a new outage worth telling the user about.
    bool fail(int64_t now) { retryAt=now+retryInterval; everDown=true; return !std::exchange(down,true); }
    // Returns true when the source came back after an outage.
    bool recover() { everUp=true; return std::exchange(down,false); }
    void disable() { down=false; retryAt=0; }
};
inline int64_t audioFrameAt(int64_t ticks) { return ticks/10000000*48000+(ticks%10000000)*48000/10000000; }
inline int64_t audioTimeAt(int64_t frame) { return frame/48000*10000000+(frame%48000)*10000000/48000; }
inline int64_t audioNearestFrame(int64_t ticks) { return int64_t(std::llround(double(ticks)*48000./10000000.)); }
// Normalize small WASAPI/QPC timestamp jitter to a continuous audio frame stream.
// Real endpoint discontinuities and gaps larger than 5 ms retain their timestamps.
// This keeps mic packet boundaries from producing tiny holes or repeated samples.
class AudioPacketAligner {
    int64_t expected_{};
    bool haveExpected_{};
public:
    uint64_t corrections{}, inferred{};
    void reset() { haveExpected_=false; }
    int64_t begin(int64_t estimated, uint32_t frames, bool timestampValid, bool discontinuity) {
        if(discontinuity) reset();
        constexpr int64_t tolerance=240; // 5 ms at 48 kHz
        if(haveExpected_ && (!timestampValid ||
            (estimated>=expected_-tolerance && estimated<=expected_+tolerance))) {
            if(!timestampValid) ++inferred;
            else if(estimated!=expected_) ++corrections;
            estimated=expected_;
        } else if(!timestampValid) ++inferred;
        expected_=estimated+int64_t(frames);
        haveExpected_=true;
        return estimated;
    }
};
// Bounded timestamped mixer. Missing packets become silence, late packets are dropped.
class AudioRing {
    struct Slot { int64_t tag=-1; float left{},right{}; };
    std::vector<Slot> slots_;
    int64_t head_{};
public:
    uint64_t dropped{}, overloadedFrames{};
    float peakBeforeLimiter{};
    explicit AudioRing(size_t capacity=96000):slots_(capacity) {}
    int64_t head() const { return head_; }
    void add(int64_t frame,float left,float right) {
        if(frame<head_ || frame>=head_+int64_t(slots_.size())) { ++dropped; return; }
        auto& s=slots_[size_t(frame)%slots_.size()];
        if(s.tag!=frame) s={frame,0,0};
        s.left+=left; s.right+=right;
    }
    static int16_t pcm(float value) {
        if(!std::isfinite(value)) return 0;
        return int16_t(std::lround(std::clamp(value,-1.f,1.f)*32767.f));
    }
    // Continuous, soft-knee protection for occasional mic+system peaks. Unlike a
    // hard clamp, this avoids the sharp corners that sound like crackles.
    static float softLimit(float value) {
        if(!std::isfinite(value)) return 0;
        constexpr float knee=.85f, range=1.f-knee;
        if(value>knee) return knee+range*std::tanh((value-knee)/range);
        if(value< -knee) return -knee-range*std::tanh((-value-knee)/range);
        return value;
    }
    std::array<int16_t,2> pop() {
        auto& s=slots_[size_t(head_)%slots_.size()];
        std::array<int16_t,2> out{};
        if(s.tag==head_) {
            float peak=std::max(std::abs(s.left),std::abs(s.right));
            peakBeforeLimiter=std::max(peakBeforeLimiter,peak);
            if(peak>1.f) ++overloadedFrames;
            out={pcm(softLimit(s.left)),pcm(softLimit(s.right))};
        }
        ++head_; return out;
    }
};
}
