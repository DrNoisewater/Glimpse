#include "audio.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <exception>
#include <thread>

namespace glimpse {
using namespace winrt;

int64_t qpcTicks() {
    LARGE_INTEGER counter,frequency;
    QueryPerformanceCounter(&counter);
    QueryPerformanceFrequency(&frequency);
    return counter.QuadPart/frequency.QuadPart*10000000+
        counter.QuadPart%frequency.QuadPart*10000000/frequency.QuadPart;
}

struct AudioRecorder::Endpoint {
    AudioRecorder& owner;
    bool loopback;
    std::atomic<bool> stop_{false};
    std::atomic<float> packetPeak_{0};
    std::thread worker_;
    mutable std::mutex statusMutex_;
    std::condition_variable readyCv_;
    bool ready_{};
    std::exception_ptr error_;
    Endpoint(AudioRecorder& recorder,bool system):owner(recorder),loopback(system) {
        worker_=std::thread([this] { run(); });
        std::unique_lock lock(statusMutex_);
        readyCv_.wait(lock,[this] { return ready_; });
        if(error_) {
            auto error=error_;
            lock.unlock();
            stop_=true;
            if(worker_.joinable()) worker_.join();
            std::rethrow_exception(error);
        }
    }
    ~Endpoint() {
        stop_=true;
        if(worker_.joinable()) worker_.join();
    }
    void announceReady() {
        {
            std::lock_guard lock(statusMutex_);
            ready_=true;
        }
        readyCv_.notify_one();
    }
    void reportError(std::exception_ptr error) {
        {
            std::lock_guard lock(statusMutex_);
            error_=error;
            ready_=true;
        }
        readyCv_.notify_one();
    }
    // S_OK while capturing; otherwise the HRESULT that stopped the worker.
    HRESULT failure() const {
        std::lock_guard lock(statusMutex_);
        return codeOf(error_);
    }
    static HRESULT codeOf(std::exception_ptr error) {
        if(!error) return S_OK;
        try { std::rethrow_exception(error); }
        catch(hresult_error const& e) { return e.code(); }
        catch(...) { return E_FAIL; }
    }
    void publishPeak(float value) {
        float previous=packetPeak_.load(std::memory_order_relaxed);
        while(value>previous && !packetPeak_.compare_exchange_weak(previous,value,std::memory_order_relaxed)) {}
    }
    float takePeak() { return packetPeak_.exchange(0,std::memory_order_relaxed); }

    void run() noexcept {
        const HRESULT com=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
        if(FAILED(com)) { reportError(std::make_exception_ptr(hresult_error(com,L"Cannot initialize the audio capture thread."))); return; }
        try {
            // Favor timely capture when the video encoder saturates a CPU core.
            SetThreadPriority(GetCurrentThread(),THREAD_PRIORITY_ABOVE_NORMAL);
            // Keep every WASAPI interface and its Stop/Release on this COM thread.
            {
                com_ptr<IMMDeviceEnumerator> enumerator;
                check_hresult(CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_ALL,IID_PPV_ARGS(enumerator.put())));
                com_ptr<IMMDevice> device;
                const HRESULT endpointResult=enumerator->GetDefaultAudioEndpoint(loopback?eRender:eCapture,eConsole,device.put());
                if(FAILED(endpointResult))
                    throw hresult_error(endpointResult);
                com_ptr<IAudioClient> client;
                check_hresult(device->Activate(__uuidof(IAudioClient),CLSCTX_ALL,nullptr,client.put_void()));
                WAVEFORMATEX format{};
                format.wFormatTag=WAVE_FORMAT_PCM;
                format.nChannels=2;
                format.nSamplesPerSec=48000;
                format.wBitsPerSample=16;
                format.nBlockAlign=4;
                format.nAvgBytesPerSec=192000;
                DWORD flags=AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM|AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY;
                if(loopback) flags|=AUDCLNT_STREAMFLAGS_LOOPBACK;
                // 500 ms WASAPI device-side cushion; the independent worker drains
                // every ~3 ms instead of waiting for an H.264 frame to finish.
                HRESULT hr=client->Initialize(AUDCLNT_SHAREMODE_SHARED,flags,5000000,0,&format,nullptr);
                // AUDCLNT_E_DEVICE_IN_USE here means another app opened the device in
                // exclusive mode; Windows will not share it until that app lets go.
                check_hresult(hr);
                com_ptr<IAudioCaptureClient> capture;
                check_hresult(client->GetService(IID_PPV_ARGS(capture.put())));
                check_hresult(client->Start());
                announceReady();
                AudioPacketAligner aligner;
                uint64_t lastEpoch=0;
                auto drain=[&] {
                    for(unsigned packet=0;packet<128;++packet) {
                        UINT32 size=0;
                        check_hresult(capture->GetNextPacketSize(&size));
                        if(!size) break;
                        BYTE* bytes=nullptr;
                        UINT32 frames=0;
                        DWORD packetFlags=0;
                        UINT64 devicePosition=0,stamp=0;
                        check_hresult(capture->GetBuffer(&bytes,&frames,&packetFlags,&devicePosition,&stamp));
                        // Always return the buffer, even if sample processing fails.
                        try {
                            if(packetFlags&AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY)
                                owner.discontinuities_.fetch_add(1,std::memory_order_relaxed);
                            const bool validStamp=!(packetFlags&AUDCLNT_BUFFERFLAGS_TIMESTAMP_ERROR);
                            if(!validStamp) owner.badTimestamps_.fetch_add(1,std::memory_order_relaxed);
                            Mapping map;
                            {
                                std::lock_guard lock(owner.mappingMutex_);
                                map=owner.mapping_;
                            }
                            if(map.epoch!=lastEpoch) {
                                aligner.reset();
                                lastEpoch=map.epoch;
                            }
                            if(map.active && frames) {
                                const int64_t qpc=validStamp?int64_t(stamp):qpcTicks()-audioTimeAt(frames);
                                const int64_t estimated=audioNearestFrame(map.anchorTime+qpc-map.anchorQpc);
                                const uint64_t oldCorrections=aligner.corrections,oldInferred=aligner.inferred;
                                const int64_t first=aligner.begin(estimated,frames,validStamp,
                                    bool(packetFlags&AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY));
                                owner.timestampCorrections_.fetch_add(aligner.corrections-oldCorrections,std::memory_order_relaxed);
                                owner.inferredTimestamps_.fetch_add(aligner.inferred-oldInferred,std::memory_order_relaxed);
                                const int64_t floorFrame=audioFrameAt(map.anchorTime);
                                float gain=(loopback?owner.systemGain_:owner.micGain_).load(std::memory_order_relaxed);
                                gain=std::clamp(gain,0.f,2.f);
                                const auto* data=reinterpret_cast<const int16_t*>(bytes);
                                float peak=0;
                                {
                                    std::lock_guard lock(owner.ringMutex_);
                                    for(UINT32 i=0;i<frames;++i) {
                                        const int64_t frame=first+int64_t(i);
                                        if(frame<floorFrame) continue;
                                        float left=0,right=0;
                                        if(!(packetFlags&AUDCLNT_BUFFERFLAGS_SILENT) && data) {
                                            left=data[i*2]/32768.f*gain;
                                            right=data[i*2+1]/32768.f*gain;
                                        }
                                        peak=std::max({peak,std::abs(left),std::abs(right)});
                                        owner.ring_.add(frame,left,right);
                                    }
                                }
                                publishPeak(peak);
                            } else aligner.reset();
                        } catch(...) {
                            capture->ReleaseBuffer(frames);
                            throw;
                        }
                        check_hresult(capture->ReleaseBuffer(frames));
                    }
                };
                try {
                    while(!stop_.load(std::memory_order_relaxed)) {
                        drain();
                        std::this_thread::sleep_for(std::chrono::milliseconds(3));
                    }
                    drain(); // Keep the last queued samples when Stop is requested.
                } catch(...) {
                    client->Stop();
                    throw;
                }
                check_hresult(client->Stop());
            }
        } catch(...) { reportError(std::current_exception()); }
        CoUninitialize();
    }
};

// ---------- TEMPORARY: Stagehand's sound over its pipe (stagehand_feed.hpp) ----------
static constexpr wchar_t StagehandPipe[]=L"\\\\.\\pipe\\stagehand-sound";

bool stagehandOffersSound() {
    // Doesn't connect: only asks whether Stagehand's pipe exists (busy still counts).
    if(WaitNamedPipeW(StagehandPipe,1)) return true;
    const DWORD e=GetLastError();
    return e!=ERROR_FILE_NOT_FOUND && e!=ERROR_PATH_NOT_FOUND && e!=ERROR_BAD_PATHNAME;
}

struct AudioRecorder::StagehandLine {
    AudioRecorder& owner;
    HANDLE pipe;
    const int64_t connectedAt;
    std::atomic<bool> stop_{false},closed_{false},everData_{false};
    std::atomic<int64_t> lastPacket_{0};
    std::atomic<float> packetPeak_{0};
    std::thread worker_;
    StagehandLine(AudioRecorder& recorder,HANDLE h):owner(recorder),pipe(h),connectedAt(qpcTicks()) {
        worker_=std::thread([this] { run(); });
    }
    ~StagehandLine() {
        stop_=true;
        if(worker_.joinable()) worker_.join();
        CloseHandle(pipe);
    }
    static std::unique_ptr<StagehandLine> open(AudioRecorder& recorder) {
        HANDLE h=CreateFileW(StagehandPipe,GENERIC_READ,0,nullptr,OPEN_EXISTING,0,nullptr);
        if(h==INVALID_HANDLE_VALUE) return nullptr;
        return std::make_unique<StagehandLine>(recorder,h);
    }
    bool closed() const { return closed_.load(); }
    // Whether Stagehand's sound is (or is about to be) recorded instead of system audio.
    // A short grace after connecting lets Stagehand's engine start sending; if nothing
    // comes (its engine isn't running), system audio takes over, and it hands back
    // the moment Stagehand's sound starts.
    bool carrying(int64_t now) const {
        if(closed()) return false;
        if(now-connectedAt<15000000) return true; // 1.5 s
        const int64_t last=lastPacket_.load();
        return last && now-last<(everData_.load()?50000000:15000000);
    }
    float takePeak() { return packetPeak_.exchange(0,std::memory_order_relaxed); }
    void run() noexcept {
        try {
            SetThreadPriority(GetCurrentThread(),THREAD_PRIORITY_ABOVE_NORMAL);
            StagehandPacketReader reader;
            StagehandResampler resampler;
            StagehandFeedClock clock;
            std::vector<uint8_t> buf(65536);
            std::vector<float> out;
            uint64_t lastEpoch=0;
            int64_t writtenEnd=0;
            while(!stop_.load(std::memory_order_relaxed)) {
                DWORD avail=0;
                if(!PeekNamedPipe(pipe,nullptr,0,nullptr,&avail,nullptr)) break; // Stagehand closed
                if(!avail) { std::this_thread::sleep_for(std::chrono::milliseconds(3)); continue; }
                DWORD got=0;
                if(!ReadFile(pipe,buf.data(),std::min<DWORD>(avail,DWORD(buf.size())),&got,nullptr)) break;
                const int64_t arrived=qpcTicks();
                reader.push(buf.data(),got,[&](const StagehandPacketReader::Packet& p) {
                    lastPacket_=arrived; everData_=true;
                    Mapping map;
                    {
                        std::lock_guard lock(owner.mappingMutex_);
                        map=owner.mapping_;
                    }
                    if(map.epoch!=lastEpoch) { clock.reset(); writtenEnd=0; lastEpoch=map.epoch; }
                    out.clear();
                    resampler.process(p.rate,p.samples.data(),p.samples.size()/2,out);
                    if(!map.active) { clock.reset(); return; }
                    const int64_t frames=int64_t(out.size()/2);
                    if(!frames) return;
                    const int64_t first=clock.place(audioNearestFrame(map.anchorTime+arrived-map.anchorQpc),frames);
                    const int64_t floorFrame=audioFrameAt(map.anchorTime);
                    const float gain=std::clamp(owner.systemGain_.load(std::memory_order_relaxed),0.f,2.f);
                    float peak=0;
                    {
                        std::lock_guard lock(owner.ringMutex_);
                        for(int64_t i=0;i<frames;++i) {
                            const int64_t frame=first+i;
                            // never add the same moment twice (after a jump back)
                            if(frame<floorFrame || frame<writtenEnd) continue;
                            const float left=out[size_t(i)*2]*gain,right=out[size_t(i)*2+1]*gain;
                            peak=std::max({peak,std::abs(left),std::abs(right)});
                            owner.ring_.add(frame,left,right);
                        }
                    }
                    writtenEnd=std::max(writtenEnd,first+frames);
                    float previous=packetPeak_.load(std::memory_order_relaxed);
                    while(peak>previous && !packetPeak_.compare_exchange_weak(previous,peak,std::memory_order_relaxed)) {}
                });
                if(reader.broken()) break;
            }
        } catch(...) {}
        closed_=true;
    }
};

void AudioRecorder::keepStagehand(bool wanted,int64_t now) {
    if(!wanted) { stagehand_.reset(); return; }
    if(stagehand_ && stagehand_->closed()) stagehand_.reset();
    if(!stagehand_ && now>=stagehandRetryAt_) {
        stagehandRetryAt_=now+20000000; // look again in 2 s
        stagehand_=StagehandLine::open(*this);
    }
}

AudioRecorder::AudioRecorder()=default;
AudioRecorder::~AudioRecorder() { close(); }

void AudioRecorder::close() {
    // Joining workers also ensures that the PCM ring and diagnostics cannot be
    // modified after close returns, before the final MP4 audio flush.
    mic_.reset();
    system_.reset();
    stagehand_.reset();
}

void AudioRecorder::configure(IMFSinkWriter* writer,bool enabled) {
    enabled_=enabled;
    if(!enabled) return;
    com_ptr<IMFMediaType> out;
    check_hresult(MFCreateMediaType(out.put()));
    check_hresult(out->SetGUID(MF_MT_MAJOR_TYPE,MFMediaType_Audio));
    check_hresult(out->SetGUID(MF_MT_SUBTYPE,MFAudioFormat_AAC));
    check_hresult(out->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS,2));
    check_hresult(out->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND,48000));
    check_hresult(out->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE,16));
    check_hresult(out->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND,16000)); // AAC 128 kbps
    check_hresult(out->SetUINT32(MF_MT_AAC_PAYLOAD_TYPE,0));
    check_hresult(writer->AddStream(out.get(),&streamIndex_));
    com_ptr<IMFMediaType> in;
    check_hresult(MFCreateMediaType(in.put()));
    check_hresult(in->SetGUID(MF_MT_MAJOR_TYPE,MFMediaType_Audio));
    check_hresult(in->SetGUID(MF_MT_SUBTYPE,MFAudioFormat_PCM));
    check_hresult(in->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS,2));
    check_hresult(in->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND,48000));
    check_hresult(in->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE,16));
    check_hresult(in->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT,4));
    check_hresult(in->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND,192000));
    check_hresult(writer->SetInputMediaType(streamIndex_,in.get(),nullptr));
}

void AudioRecorder::collect(unsigned mode,const Timeline* timeline,int64_t activeFrom,bool paused,float micGain,float systemGain) {
    if(!enabled_) return;
    micGain_=micGain;
    systemGain_=systemGain;
    // This is now a fast control/update operation; audio capture never waits
    // for the video encoder, and this thread never drains a WASAPI endpoint.
    {
        std::lock_guard lock(mappingMutex_);
        const bool active=(timeline!=nullptr && !paused);
        if(mapping_.active!=active || (active && mapping_.anchorQpc!=activeFrom)) ++mapping_.epoch;
        mapping_.active=active;
        if(active) {
            mapping_.anchorQpc=activeFrom;
            mapping_.anchorTime=timeline->at(activeFrom);
        }
    }
    const int64_t now=qpcTicks();
    keepStagehand(mode&AudioSystem,now);
    const bool viaStagehand=stagehand_ && stagehand_->carrying(now);
    if(viaStagehand && !stagehandTold_ && stagehand_->everData_.load()) {
        stagehandTold_=true;
        notices_.push_back(L"Stagehand is open: its sound is being recorded.");
    }
    keep(mic_,micHealth_,false,mode&AudioMic);
    keep(system_,systemHealth_,true,(mode&AudioSystem) && !viaStagehand);
    micLevel_=(mode&AudioMic && mic_)?std::max(micLevel_*.85f,mic_->takePeak()):0.f;
    const float systemPeak=viaStagehand?stagehand_->takePeak():system_?system_->takePeak():0.f;
    systemLevel_=(mode&AudioSystem)?std::max(systemLevel_*.85f,systemPeak):0.f;
}

void AudioRecorder::keep(std::unique_ptr<Endpoint>& endpoint,AudioSourceHealth& health,bool system,bool wanted) {
    if(!wanted) { endpoint.reset(); health.disable(); return; }
    const int64_t now=qpcTicks();
    HRESULT failure=S_OK;
    if(endpoint) {
        failure=endpoint->failure();
        if(SUCCEEDED(failure)) return;
        endpoint.reset(); // Lost mid-recording (unplugged, or taken over in exclusive mode).
    } else {
        if(!health.shouldTry(now)) return;
        try {
            endpoint=std::make_unique<Endpoint>(*this,system);
            if(health.recover())
                notices_.push_back(system?L"System audio is available again and is being recorded."
                                         :L"The microphone is available again and is being recorded.");
            return;
        } catch(...) { failure=Endpoint::codeOf(std::current_exception()); }
    }
    if(health.fail(now)) notices_.push_back(audioProblemText(system,uint32_t(failure)));
}

void AudioRecorder::writeUntil(IMFSinkWriter* writer,int64_t endTime) {
    if(!enabled_) return;
    const int64_t end=audioFrameAt(std::max<int64_t>(0,endTime));
    while(true) {
        int64_t first=0;
        DWORD frames=0;
        // AAC-LC encodes 1024 input frames per output access unit. Larger
        // chunks also reduce synchronous sink-writer overhead during capture.
        std::array<std::array<int16_t,2>,1024> mixed{};
        {
            std::lock_guard lock(ringMutex_);
            if(ring_.head()>=end) break;
            first=ring_.head();
            frames=DWORD(std::min<int64_t>(int64_t(mixed.size()),end-first));
            for(DWORD i=0;i<frames;++i) mixed[i]=ring_.pop();
        }
        com_ptr<IMFMediaBuffer> buffer;
        check_hresult(MFCreateMemoryBuffer(frames*4,buffer.put()));
        BYTE* bytes=nullptr;
        check_hresult(buffer->Lock(&bytes,nullptr,nullptr));
        std::memcpy(bytes,mixed.data(),size_t(frames)*4);
        check_hresult(buffer->Unlock());
        check_hresult(buffer->SetCurrentLength(frames*4));
        com_ptr<IMFSample> sample;
        check_hresult(MFCreateSample(sample.put()));
        check_hresult(sample->AddBuffer(buffer.get()));
        check_hresult(sample->SetSampleTime(audioTimeAt(first)));
        check_hresult(sample->SetSampleDuration(audioTimeAt(first+frames)-audioTimeAt(first)));
        check_hresult(writer->WriteSample(streamIndex_,sample.get()));
    }
}
}
