#include "policy.hpp"
#include <cstdlib>
#include <iostream>
using namespace glimpse;
int checks=0;
void expect(bool condition,const char* label) {
    ++checks; if(!condition) { std::cerr<<"FAILED: "<<label<<'\n'; std::exit(1); }
}
int main() {
    auto selection=normalize(120,900,-1920,-40);
    expect(selection.left==-1920&&selection.top==-40&&selection.width()==2040&&selection.height()==940,"reverse drag across negative desktop coordinates");
    auto clip=intersect(selection,{-1920,0,0,1080});
    expect(clip.left==-1920&&clip.top==0&&clip.right==0&&clip.bottom==900,"monitor source crop");
    auto output=videoSpec(3840,2160,Preset::Share,0);
    expect(output.width==1920&&output.height==1080&&output.fps==30,"4K share ceiling");
    output=videoSpec(1440,2560,Preset::Share,0);
    expect(output.width==1080&&output.height==1920,"portrait share ceiling");
    output=videoSpec(799,599,Preset::Share,0);
    expect(output.width==798&&output.height==598,"even dimensions without upscaling");
    output=videoSpec(7680,1440,Preset::Share,0);
    expect(output.width==1920&&output.height==360,"ultrawide spanning aspect ratio");
    output=videoSpec(3840,2160,Preset::High,0);
    expect(output.width==3840&&output.height==2160&&output.fps==60,"high quality 4K60");
    output=videoSpec(3840,2160,Preset::Small,60);
    expect(output.width==1280&&output.height==720&&output.fps==15,"small file preset caps fps");
    output=videoSpec(800,600,Preset::High,15);
    expect(output.fps==15,"explicit fps cap");
    for(int w:{2,3,33,800,1920,16384}) for(int h:{2,3,35,600,1080,16384})
        for(auto p:{Preset::Share,Preset::High,Preset::Small}) {
            auto v=videoSpec(w,h,p,0);
            expect(v.width>=2&&v.height>=2&&v.width%2==0&&v.height%2==0,"encoder dimension constraints");
            expect(v.width<=w&&v.height<=h,"never upscale");
            expect(v.bitrate>=150000&&v.bitrate<=40000000,"bounded fallback bitrate");
        }
    for(auto dims:{Rect{0,0,0,100},Rect{0,0,100,1},Rect{0,0,20000,100}}) {
        bool caught=false; try { videoSpec(dims.width(),dims.height(),Preset::Share,0); } catch(const std::invalid_argument&) { caught=true; }
        expect(caught,"reject invalid capture size");
    }
    Timeline t(100);
    expect(t.at(250)==150,"monotonic elapsed");
    t.pause(300); expect(t.at(900)==200,"pause freezes time");
    t.pause(400); t.resume(1000); expect(t.at(1100)==300,"duplicate pause does not shift origin");
    t.resume(1200); expect(t.at(1300)==500,"duplicate resume ignored");
    t.pause(1400); t.resume(2000); expect(t.at(2100)==700,"multiple pauses excluded");
    std::cout<<checks<<" policy checks passed\n";
}
