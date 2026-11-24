#include "audio_policy.hpp"
#include "policy.hpp"
#include <cstdlib>
#include <iostream>
#include <limits>
using namespace glimpse;
int checks=0;
void expect(bool condition,const char* label) {
    ++checks; if(!condition) { std::cerr<<"FAILED: "<<label<<'\n'; std::exit(1); }
}
int main() {
    AudioRing r(16);
    r.add(0,.25f,-.25f); r.add(0,.5f,-.5f);
    auto v=r.pop(); expect(v[0]==24575&&v[1]==-24575,"mic and system mixing");
    v=r.pop(); expect(v[0]==0&&v[1]==0,"absent system packets become silence");
    r.add(2,4.f,-4.f); v=r.pop(); expect(v[0]==32767&&v[1]==-32767,"saturation protects PCM range");
    r.add(2,1,1); expect(r.dropped==1,"late packets do not rewrite emitted audio");
    r.add(40,1,1); expect(r.dropped==2,"future packets cannot grow memory");
    expect(AudioRing::pcm(std::numeric_limits<float>::quiet_NaN())==0,"invalid sample becomes silence");
    expect(AudioRing::pcm(.5f*.65f)==10649,"65 percent source volume");
    expect(AudioRing::softLimit(.25f)==.25f,"quiet audio retains its original level");
    expect(AudioRing::softLimit(1.5f)<1.f&&AudioRing::softLimit(1.5f)>.9f,"soft limiter reduces harsh clipping");
    AudioRing mix(8);
    mix.add(0,.9f,.9f); mix.add(0,.5f,.5f);
    auto limited=mix.pop();
    expect(limited[0]>0&&limited[0]<32767,"microphone and system peaks smoothly limited");
    expect(mix.overloadedFrames==1&&mix.peakBeforeLimiter>1.f,"audio overload is measured");
    AudioPacketAligner aligner;
    expect(aligner.begin(1000,480,true,false)==1000,"first packet uses device timestamp");
    expect(aligner.begin(1482,480,true,false)==1480,"small timestamp jitter does not create audio gaps");
    expect(aligner.corrections==1,"jitter corrections are counted");
    expect(aligner.begin(1234,480,false,false)==1960,"bad timestamp retains contiguous audio");
    expect(aligner.inferred==1,"bad timestamps are counted");
    expect(aligner.begin(3000,480,true,true)==3000,"discontinuities preserve real gaps");
    aligner.reset();
    expect(aligner.begin(5000,480,true,false)==5000,"pause clears old continuity");
    for(int i=0;i<10000;++i) {
        expect(audioNearestFrame(audioTimeAt(i))==i,"48k timestamp quantization preserves sample index");
    }
    expect(classifyAudioFailure(0x8889000Au)==AudioProblem::InUse,"exclusive-mode owner is recognised");
    expect(classifyAudioFailure(0x80070005u)==AudioProblem::Blocked,"privacy block is recognised");
    expect(classifyAudioFailure(0x88890004u)==AudioProblem::Lost,"mid-recording takeover is recognised");
    expect(audioProblemText(false,0x8889000Au).find(L"exclusive control of the microphone")!=std::wstring::npos,"in-use mic text names the cause");
    expect(audioProblemText(true,0x12345678u).find(L"0x12345678")!=std::wstring::npos,"unknown errors keep their code");
    AudioSourceHealth health;
    expect(health.shouldTry(0),"a new source is opened immediately");
    expect(health.fail(100),"first failure is announced");
    expect(!health.shouldTry(101),"failed source waits before retrying");
    expect(!health.fail(AudioSourceHealth::retryInterval+100),"repeat failures are not re-announced");
    expect(health.shouldTry(2*AudioSourceHealth::retryInterval+100),"failed source is retried");
    expect(health.recover()&&health.everDown&&health.everUp,"recovery is announced and remembered");
    expect(!health.recover(),"healthy source is not re-announced");
    for(int i=0;i<100;++i) { r.add(r.head(),.1f,.2f); v=r.pop(); expect(v[0]==3277&&v[1]==6553,"ring reuse never leaks old audio"); }
    Timeline t(10000000); // start at one second on QPC clock
    int64_t floor=10000000, packet=9990000;
    auto first=audioNearestFrame(t.at(floor)+packet-floor);
    expect(first==-48,"packet before start is trimmed rather than shifted into recording");
    t.pause(20000000); t.resume(50000000); floor=50000000;
    first=audioNearestFrame(t.at(floor)+49990000-floor);
    expect(first==47952,"resume uses pause-adjusted timeline for old packets");
    expect(audioFrameAt(36000000000LL)==172800000,"one hour audio clock does not overflow");
    expect(audioTimeAt(172800000)==36000000000LL,"one hour round trip");
    std::cout<<checks<<" audio checks passed\n";
}
