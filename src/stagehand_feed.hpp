#pragma once
// TEMPORARY: Stagehand's sound, handed over a local pipe.
//
// Stagehand (a guitar app) takes the speakers in exclusive mode, so Windows'
// loopback capture records silence. While it runs it serves its own mix on
// \\.\pipe\stagehand-sound; Glimpse reads it in place of system audio.
// Portable parts (packet parsing, resampling to 48 kHz, timing) live here so
// they can be tested on any machine.
//
// Each packet, little-endian: u32 'SHS1', u32 rate, u32 frames, u32 0,
// then frames x 2 float32 (left, right).
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace glimpse {
constexpr uint32_t StagehandMagic=0x31534853; // "SHS1"

// Splits the byte stream into packets. A bad header means the stream can't be
// trusted any more: broken() turns true and the caller hangs up.
class StagehandPacketReader {
    std::vector<uint8_t> buf_;
    bool broken_{};
    static uint32_t u32(const uint8_t* p) { return uint32_t(p[0])|uint32_t(p[1])<<8|uint32_t(p[2])<<16|uint32_t(p[3])<<24; }
public:
    struct Packet { uint32_t rate{}; std::vector<float> samples; }; // interleaved L/R
    bool broken() const { return broken_; }
    // Adds bytes; calls onPacket(const Packet&) for each complete packet.
    template<class F> void push(const uint8_t* data,size_t size,F&& onPacket) {
        if(broken_) return;
        buf_.insert(buf_.end(),data,data+size);
        size_t off=0;
        while(buf_.size()-off>=16) {
            const uint8_t* h=buf_.data()+off;
            const uint32_t magic=u32(h),rate=u32(h+4),frames=u32(h+8);
            if(magic!=StagehandMagic || rate<8000 || rate>384000 || frames>96000) { broken_=true; buf_.clear(); return; }
            const size_t bytes=16+size_t(frames)*8;
            if(buf_.size()-off<bytes) break;
            Packet p; p.rate=rate; p.samples.resize(size_t(frames)*2);
            if(frames) std::memcpy(p.samples.data(),h+16,size_t(frames)*8);
            for(auto& s:p.samples) if(!std::isfinite(s)) s=0;
            onPacket(p);
            off+=bytes;
        }
        buf_.erase(buf_.begin(),buf_.begin()+ptrdiff_t(off));
    }
};

// Continuous linear resampler to 48 kHz, carried across packets.
class StagehandResampler {
    uint32_t rate_{};
    double pos_{}; // read position relative to the first sample of the next packet (may be -1..0)
    float lastL_{},lastR_{};
    bool haveLast_{};
public:
    void reset() { rate_=0; pos_=0; haveLast_=false; }
    // Appends 48 kHz interleaved frames to out.
    void process(uint32_t rate,const float* in,size_t frames,std::vector<float>& out) {
        if(!frames) return;
        if(rate!=rate_) { reset(); rate_=rate; }
        if(rate==48000) {
            out.insert(out.end(),in,in+frames*2);
            lastL_=in[frames*2-2]; lastR_=in[frames*2-1]; haveLast_=true; pos_=0;
            return;
        }
        const double step=double(rate)/48000.;
        if(!haveLast_) { pos_=0; }
        // sample k in [-1, frames-1]; k=-1 is the previous packet's last sample
        auto at=[&](long k,int c) { return k<0?(c?lastR_:lastL_):in[size_t(k)*2+size_t(c)]; };
        while(pos_<=double(frames-1)) {
            const long k=long(std::floor(pos_));
            const float t=float(pos_-double(k));
            const long k1=k+1;
            for(int c=0;c<2;++c) {
                const float a=at(k,c),b=k1<=long(frames-1)?at(k1,c):a;
                out.push_back(a+(b-a)*t);
            }
            pos_+=step;
        }
        pos_-=double(frames);
        lastL_=in[frames*2-2]; lastR_=in[frames*2-1]; haveLast_=true;
    }
};

// Places packets on the recording's 48 kHz frame line by when they arrive.
// The pipe adds jitter of a few tens of ms, so small differences keep the sound
// continuous; only a real gap (Stagehand paused its engine, or the line stalled)
// or drift past the tolerance moves it.
class StagehandFeedClock {
    int64_t expected_{};
    bool have_{};
public:
    static constexpr int64_t tolerance=4800; // 100 ms at 48 kHz
    uint64_t resyncs{};
    void reset() { have_=false; }
    // arrivedEnd: the recording frame at which this packet's last sample arrived.
    int64_t place(int64_t arrivedEnd,int64_t frames) {
        const int64_t estimated=arrivedEnd-frames;
        if(have_ && estimated>=expected_-tolerance && estimated<=expected_+tolerance) {
            const int64_t first=expected_;
            expected_+=frames;
            return first;
        }
        if(have_) ++resyncs;
        have_=true;
        expected_=estimated+frames;
        return estimated;
    }
};
}
