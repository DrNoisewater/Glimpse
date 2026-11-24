#include "policy.hpp"
#include "update_policy.hpp"
#include <cstdlib>
#include <iostream>
#include <string>
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
    expect(parseVersion("v0.3.2")==Version{0,3,2},"release tag parses");
    expect(parseVersion("10.20.30")==Version{10,20,30},"plain version parses");
    expect(!parseVersion("v1.2")&&!parseVersion("1.2.3-beta")&&!parseVersion("1..3")&&!parseVersion(""),"malformed versions rejected");
    expect(isNewer(Version{0,4,0},Version{0,3,9})&&isNewer(Version{0,3,10},Version{0,3,9}),"newer release detected numerically");
    expect(!isNewer(Version{0,3,2},Version{0,3,2})&&!isNewer(Version{0,3,1},Version{0,3,2}),"same or older is not an update");
    expect(versionText(Version{1,0,12})=="1.0.12","version text");
    expect(releaseTag(R"({"url":"x","tag_name" : "v0.4.0","name":"Glimpse v0.4.0"})")==std::string("v0.4.0"),"tag read from release JSON");
    expect(!releaseTag(R"({"message":"Not Found"})"),"missing release has no tag");
    expect(installerName(Version{0,4,0})=="Glimpse-Setup-0.4.0.exe","installer asset name");
    {
        const std::string hex(64,'a'), HEX(64,'A');
        const std::string json=R"({"tag_name":"v0.4.0","assets":[{"name":"Glimpse-0.4.0-portable.zip","digest":"sha256:)"+std::string(64,'b')+
            R"(","browser_download_url":"z"},{"name":"Glimpse-Setup-0.4.0.exe","uploader":{"login":"x"},"digest":"sha256:)"+HEX+
            R"(","browser_download_url":"y"}]})";
        expect(assetSha256(json,"Glimpse-Setup-0.4.0.exe")==hex,"installer digest is read from its own asset, lowercased");
        expect(assetSha256(json,"Glimpse-0.4.0-portable.zip")==std::string(64,'b'),"each asset keeps its own digest");
        expect(!assetSha256(json,"Glimpse-Setup-0.5.0.exe"),"missing asset has no digest");
        const std::string noDigest=R"({"assets":[{"name":"Glimpse-Setup-0.4.0.exe","browser_download_url":"y"},{"name":"b","digest":"sha256:)"+hex+R"("}]})";
        expect(!assetSha256(noDigest,"Glimpse-Setup-0.4.0.exe"),"another asset's digest is never borrowed");
        expect(!assetSha256(R"({"name":"Glimpse-Setup-0.4.0.exe","digest":"sha256:abc","browser_download_url":"y"})","Glimpse-Setup-0.4.0.exe"),"short digest rejected");
    }
    std::cout<<checks<<" policy checks passed\n";
}
