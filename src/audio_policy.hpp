#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>
namespace glimpse {
enum AudioMode : unsigned { AudioSilent=0, AudioMic=1, AudioSystem=2, AudioBoth=3 };
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
