#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>

namespace glimpse {
struct Rect {
    int left{}, top{}, right{}, bottom{};
    int width() const { return right - left; }
    int height() const { return bottom - top; }
};
inline Rect normalize(int x1, int y1, int x2, int y2) {
    return {std::min(x1,x2), std::min(y1,y2), std::max(x1,x2), std::max(y1,y2)};
}
inline Rect intersect(Rect a, Rect b) {
    return {std::max(a.left,b.left),std::max(a.top,b.top),
            std::min(a.right,b.right),std::min(a.bottom,b.bottom)};
}
enum class Preset { Share, High, Small };
struct VideoSpec { int width, height, fps, bitrate, quality; };
inline VideoSpec videoSpec(int w, int h, Preset preset, int preferredFps) {
    if(w < 2 || h < 2 || w > 16384 || h > 16384)
        throw std::invalid_argument("Select a region between 2 and 16384 pixels per side.");
    const int longEdge = preset == Preset::High ? 3840 : preset == Preset::Small ? 1280 : 1920;
    const int shortEdge = preset == Preset::High ? 2160 : preset == Preset::Small ? 720 : 1080;
    double scale = std::min({1.0, double(longEdge)/std::max(w,h), double(shortEdge)/std::min(w,h)});
    int ow = std::max(2, int(w*scale)/2*2), oh = std::max(2,int(h*scale)/2*2);
    int fps = preset == Preset::Small ? 15 : preset == Preset::High ? 60 : 30;
    if(preferredFps > 0) fps = std::min(fps, preferredFps);
    const double bpp = preset == Preset::High ? .12 : preset == Preset::Small ? .07 : .085;
    int bitrate = std::clamp(int(double(ow)*oh*fps*bpp), 150000, 40000000);
    return {ow,oh,fps,bitrate,preset == Preset::High ? 90 : preset == Preset::Small ? 60 : 78};
}
// Times are integer 100 ns units. Each transition is supplied with one monotonic timestamp.
class Timeline {
    int64_t start_{}, pauseAt_{}, pausedTotal_{};
    bool paused_{};
public:
    explicit Timeline(int64_t now) : start_(now) {}
    void pause(int64_t now) { if(!paused_) { pauseAt_=now; paused_=true; } }
    void resume(int64_t now) { if(paused_) { pausedTotal_+=now-pauseAt_; paused_=false; } }
    int64_t at(int64_t now) const { return std::max<int64_t>(0,(paused_?pauseAt_:now)-start_-pausedTotal_); }
};
}
