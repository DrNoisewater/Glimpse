#include "capture.hpp"
#include "audio.hpp"
#include <d3d11.h>
#include <d3d10.h>
#include <dxgi1_2.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <codecapi.h>
#include <strmif.h>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <chrono>
#include <fstream>
#include <vector>
#include <memory>
#include <sstream>
#include <iomanip>

namespace glimpse {
using namespace winrt;
using namespace winrt::Windows::Graphics::Capture;
using namespace winrt::Windows::Graphics::DirectX;
using namespace winrt::Windows::Graphics::DirectX::Direct3D11;
using Clock = std::chrono::steady_clock;
static int64_t ticks() { return qpcTicks(); }
static void checked(HRESULT hr, const wchar_t* action) {
    if(FAILED(hr)) throw hresult_error(hr, action);
}
static Rect rect(RECT r) { return {r.left,r.top,r.right,r.bottom}; }
struct Monitor { HMONITOR handle; Rect bounds; };
static BOOL CALLBACK enumerate(HMONITOR m, HDC, LPRECT r, LPARAM data) {
    static_cast<std::vector<Monitor>*>(reinterpret_cast<void*>(data))->push_back({m,rect(*r)}); return TRUE;
}
struct Source {
    Monitor monitor;
    GraphicsCaptureItem item{nullptr};
    Direct3D11CaptureFramePool pool{nullptr};
    GraphicsCaptureSession session{nullptr};
    event_token closedToken{};
    std::shared_ptr<std::atomic<bool>> closed = std::make_shared<std::atomic<bool>>(false);
    bool received{};
    ~Source() {
        try { if(session) session.Close(); if(pool) pool.Close(); if(item) item.Closed(closedToken); } catch(...) {}
    }
};
struct Graphics {
    com_ptr<ID3D11Device> device;
    com_ptr<ID3D11DeviceContext> context;
    com_ptr<ID3D11VideoDevice> videoDevice;
    com_ptr<ID3D11VideoContext> videoContext;
    com_ptr<ID3D11VideoProcessorEnumerator> enumerator;
    com_ptr<ID3D11VideoProcessor> processor;
    com_ptr<ID3D11Texture2D> canvas, staging;
    com_ptr<ID3D11VideoProcessorInputView> inputView;
    IDirect3DDevice winrtDevice{nullptr};
    Rect area;
    VideoSpec spec;
    Graphics(Rect r, VideoSpec s) : area(r), spec(s) {
        UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_VIDEO_SUPPORT;
        D3D_FEATURE_LEVEL level;
        HRESULT hr = D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,flags,nullptr,0,
            D3D11_SDK_VERSION,device.put(),&level,context.put());
        if(FAILED(hr)) {
            device=nullptr; context=nullptr;
            checked(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,flags,nullptr,0,
                D3D11_SDK_VERSION,device.put(),&level,context.put()), L"Cannot create the Windows graphics device.");
        }
        // Media Foundation may use this device on another thread.
        auto multi = device.as<ID3D10Multithread>(); multi->SetMultithreadProtected(TRUE);
        auto dxgi = device.as<IDXGIDevice>();
        com_ptr<IInspectable> inspectable;
        check_hresult(CreateDirect3D11DeviceFromDXGIDevice(dxgi.get(),inspectable.put()));
        winrtDevice = inspectable.as<IDirect3DDevice>();
        videoDevice=device.as<ID3D11VideoDevice>(); videoContext=context.as<ID3D11VideoContext>();
        D3D11_TEXTURE2D_DESC td{};
        td.Width=r.width(); td.Height=r.height(); td.MipLevels=td.ArraySize=1;
        td.Format=DXGI_FORMAT_B8G8R8A8_UNORM; td.SampleDesc.Count=1;
        td.Usage=D3D11_USAGE_DEFAULT; td.BindFlags=D3D11_BIND_RENDER_TARGET;
        checked(device->CreateTexture2D(&td,nullptr,canvas.put()),L"The selected region is too large for this graphics device.");
        com_ptr<ID3D11RenderTargetView> target;
        check_hresult(device->CreateRenderTargetView(canvas.get(),nullptr,target.put()));
        float black[4]{0,0,0,1}; context->ClearRenderTargetView(target.get(),black);
        D3D11_VIDEO_PROCESSOR_CONTENT_DESC cd{};
        cd.InputFrameFormat=D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
        cd.InputWidth=td.Width; cd.InputHeight=td.Height;
        cd.OutputWidth=s.width; cd.OutputHeight=s.height;
        cd.InputFrameRate={UINT(s.fps),1}; cd.OutputFrameRate=cd.InputFrameRate;
        cd.Usage=D3D11_VIDEO_USAGE_PLAYBACK_NORMAL;
        check_hresult(videoDevice->CreateVideoProcessorEnumerator(&cd,enumerator.put()));
        UINT support=0; check_hresult(enumerator->CheckVideoProcessorFormat(DXGI_FORMAT_NV12,&support));
        if(!(support & D3D11_VIDEO_PROCESSOR_FORMAT_SUPPORT_OUTPUT))
            throw hresult_error(E_FAIL,L"The graphics driver cannot convert video to NV12. Update the display driver.");
        check_hresult(videoDevice->CreateVideoProcessor(enumerator.get(),0,processor.put()));
        D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC iv{}; iv.ViewDimension=D3D11_VPIV_DIMENSION_TEXTURE2D;
        check_hresult(videoDevice->CreateVideoProcessorInputView(canvas.get(),enumerator.get(),&iv,inputView.put()));
        RECT source{0,0,r.width(),r.height()}, destination{0,0,s.width,s.height};
        videoContext->VideoProcessorSetStreamSourceRect(processor.get(),0,TRUE,&source);
        videoContext->VideoProcessorSetStreamDestRect(processor.get(),0,TRUE,&destination);
        videoContext->VideoProcessorSetOutputTargetRect(processor.get(),TRUE,&destination);
        videoContext->VideoProcessorSetStreamFrameFormat(processor.get(),0,D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE);
        videoContext->VideoProcessorSetStreamAutoProcessingMode(processor.get(),0,FALSE);
        D3D11_VIDEO_PROCESSOR_COLOR_SPACE rgb{}, yuv{};
        rgb.RGB_Range=0; rgb.YCbCr_Matrix=1; rgb.Nominal_Range=2; // full RGB
        yuv.YCbCr_Matrix=1; yuv.Nominal_Range=1; // limited-range BT.709
        videoContext->VideoProcessorSetStreamColorSpace(processor.get(),0,&rgb);
        videoContext->VideoProcessorSetOutputColorSpace(processor.get(),&yuv);
    }
    com_ptr<ID3D11Texture2D> convert() {
        D3D11_TEXTURE2D_DESC td{};
        td.Width=spec.width; td.Height=spec.height; td.MipLevels=td.ArraySize=1;
        td.Format=DXGI_FORMAT_NV12; td.SampleDesc.Count=1; td.Usage=D3D11_USAGE_DEFAULT;
        td.BindFlags=D3D11_BIND_RENDER_TARGET;
        com_ptr<ID3D11Texture2D> texture;
        check_hresult(device->CreateTexture2D(&td,nullptr,texture.put()));
        com_ptr<ID3D11VideoProcessorOutputView> out;
        D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC od{}; od.ViewDimension=D3D11_VPOV_DIMENSION_TEXTURE2D;
        check_hresult(videoDevice->CreateVideoProcessorOutputView(texture.get(),enumerator.get(),&od,out.put()));
        D3D11_VIDEO_PROCESSOR_STREAM stream{}; stream.Enable=TRUE; stream.pInputSurface=inputView.get();
        check_hresult(videoContext->VideoProcessorBlt(processor.get(),out.get(),0,1,&stream));
        return texture; // Each sample owns a distinct surface; encoder-retained frames cannot be overwritten.
    }
    com_ptr<IMFMediaBuffer> buffer(ID3D11Texture2D* texture, bool gpu) {
        com_ptr<IMFMediaBuffer> result;
        if(gpu) {
            check_hresult(MFCreateDXGISurfaceBuffer(__uuidof(ID3D11Texture2D),texture,0,FALSE,result.put()));
            auto twoD=result.as<IMF2DBuffer>(); DWORD size=0;
            check_hresult(twoD->GetContiguousLength(&size)); check_hresult(result->SetCurrentLength(size));
        } else {
            if(!staging) {
                D3D11_TEXTURE2D_DESC d; texture->GetDesc(&d);
                d.BindFlags=0; d.Usage=D3D11_USAGE_STAGING; d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
                check_hresult(device->CreateTexture2D(&d,nullptr,staging.put()));
            }
            context->CopyResource(staging.get(),texture);
            D3D11_MAPPED_SUBRESOURCE mapped{};
            check_hresult(context->Map(staging.get(),0,D3D11_MAP_READ,0,&mapped));
            try {
                DWORD size=DWORD(spec.width*spec.height*3/2);
                check_hresult(MFCreateMemoryBuffer(size,result.put()));
                BYTE* bytes=nullptr; check_hresult(result->Lock(&bytes,nullptr,nullptr));
                for(int y=0;y<spec.height*3/2;++y)
                    memcpy(bytes+size_t(y)*spec.width,static_cast<BYTE*>(mapped.pData)+size_t(y)*mapped.RowPitch,spec.width);
                result->Unlock(); check_hresult(result->SetCurrentLength(size));
            } catch(...) { context->Unmap(staging.get(),0); throw; }
            context->Unmap(staging.get(),0);
        }
        return result;
    }
};
struct Encoder {
    com_ptr<IMFSinkWriter> writer;
    com_ptr<IMFByteStream> stream;
    com_ptr<IMFDXGIDeviceManager> manager;
    DWORD index{}; bool gpu{}, quality{};
    void open(const std::filesystem::path& path, Graphics& g, bool useGpu, AudioRecorder& audio, bool withAudio) {
        gpu=useGpu; quality=false;
        // FAIL_IF_EXIST prevents touching any earlier capture, including a prior partial file.
        checked(MFCreateFile(MF_ACCESSMODE_READWRITE,MF_OPENMODE_FAIL_IF_EXIST,MF_FILEFLAGS_NONE,
            path.c_str(),stream.put()),L"Cannot create the recording. Check folder access and free space.");
        com_ptr<IMFAttributes> attributes; check_hresult(MFCreateAttributes(attributes.put(),6));
        check_hresult(attributes->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS,gpu));
        if(gpu) {
            UINT token=0; check_hresult(MFCreateDXGIDeviceManager(&token,manager.put()));
            check_hresult(manager->ResetDevice(g.device.get(),token));
            check_hresult(attributes->SetUnknown(MF_SINK_WRITER_D3D_MANAGER,manager.get()));
        }
        // URL chooses the MPEG-4 sink; bytes are written only to the already-reserved partial file.
        check_hresult(MFCreateSinkWriterFromURL(L"capture.mp4",stream.get(),attributes.get(),writer.put()));
        com_ptr<IMFMediaType> output; check_hresult(MFCreateMediaType(output.put()));
        check_hresult(output->SetGUID(MF_MT_MAJOR_TYPE,MFMediaType_Video));
        check_hresult(output->SetGUID(MF_MT_SUBTYPE,MFVideoFormat_H264));
        check_hresult(output->SetUINT32(MF_MT_AVG_BITRATE,g.spec.bitrate));
        check_hresult(output->SetUINT32(MF_MT_INTERLACE_MODE,MFVideoInterlace_Progressive));
        check_hresult(output->SetUINT32(MF_MT_MPEG2_PROFILE,eAVEncH264VProfile_Main));
        check_hresult(MFSetAttributeSize(output.get(),MF_MT_FRAME_SIZE,g.spec.width,g.spec.height));
        check_hresult(MFSetAttributeRatio(output.get(),MF_MT_FRAME_RATE,g.spec.fps,1));
        check_hresult(MFSetAttributeRatio(output.get(),MF_MT_PIXEL_ASPECT_RATIO,1,1));
        check_hresult(writer->AddStream(output.get(),&index));
        com_ptr<IMFMediaType> input; check_hresult(MFCreateMediaType(input.put()));
        check_hresult(output->CopyAllItems(input.get()));
        input->DeleteItem(MF_MT_AVG_BITRATE); input->DeleteItem(MF_MT_MPEG2_PROFILE);
        check_hresult(input->SetGUID(MF_MT_SUBTYPE,MFVideoFormat_NV12));
        check_hresult(input->SetUINT32(MF_MT_VIDEO_NOMINAL_RANGE,MFNominalRange_16_235));
        check_hresult(input->SetUINT32(MF_MT_YUV_MATRIX,MFVideoTransferMatrix_BT709));
        check_hresult(writer->SetInputMediaType(index,input.get(),nullptr));
        com_ptr<ICodecAPI> codec;
        if(SUCCEEDED(writer->GetServiceForStream(index,GUID_NULL,__uuidof(ICodecAPI),codec.put_void()))) {
            VARIANT mode; VariantInit(&mode); mode.vt=VT_UI4; mode.ulVal=eAVEncCommonRateControlMode_Quality;
            if(SUCCEEDED(codec->SetValue(&CODECAPI_AVEncCommonRateControlMode,&mode))) {
                VARIANT qualityValue=mode; qualityValue.ulVal=g.spec.quality;
                quality=SUCCEEDED(codec->SetValue(&CODECAPI_AVEncCommonQuality,&qualityValue));
                if(!quality) {
                    mode.ulVal=eAVEncCommonRateControlMode_CBR;
                    codec->SetValue(&CODECAPI_AVEncCommonRateControlMode,&mode);
                }
            }
        }
        audio.configure(writer.get(),withAudio);
        check_hresult(writer->BeginWriting());
    }
    void write(IMFSample* sample, int64_t duration) {
        check_hresult(sample->SetSampleDuration(std::max<int64_t>(1,duration)));
        checked(writer->WriteSample(index,sample),L"Windows could not encode a frame. The disk may be full or the encoder may have failed.");
    }
    void finish() { checked(writer->Finalize(),L"Windows could not finalize the MP4. The partial file has been kept."); writer=nullptr; stream->Close(); stream=nullptr; }
    void close() { writer=nullptr; if(stream) stream->Close(); stream=nullptr; manager=nullptr; }
    ~Encoder() { close(); }
};
static std::filesystem::path choosePath(const std::filesystem::path& folder) {
    std::filesystem::create_directories(folder);
    SYSTEMTIME t; GetLocalTime(&t); wchar_t stem[100];
    swprintf_s(stem,L"Capture_%04u-%02u-%02u_%02u%02u%02u_%03u",t.wYear,t.wMonth,t.wDay,t.wHour,t.wMinute,t.wSecond,t.wMilliseconds);
    for(unsigned i=0;i<10000;++i) {
        auto p=folder/(std::wstring(stem)+(i?L"_"+std::to_wstring(i):L"")+L".mp4");
        if(!std::filesystem::exists(p) && !std::filesystem::exists(p.wstring()+L".partial")) return p;
    }
    throw hresult_error(E_FAIL,L"Cannot find an unused capture filename.");
}
void Recorder::run(HWND owner, Rect area, VideoSpec spec, std::filesystem::path folder, bool forceSoftware, unsigned initialAudio) noexcept {
    auto result=std::make_unique<CaptureResult>();
    bool apartment=false, mf=false, opened=false;
    const int64_t requested=ticks();
    Encoder encoder;
    AudioRecorder audio;
    com_ptr<IMFSample> pending;
    int64_t pendingTime=0, duration=0;
    try {
        init_apartment(apartment_type::multi_threaded); apartment=true;
        check_hresult(MFStartup(MF_VERSION,MFSTARTUP_FULL)); mf=true;
        if(!GraphicsCaptureSession::IsSupported()) throw hresult_error(E_NOTIMPL,L"Windows screen capture is unavailable on this system.");
        result->file=choosePath(folder); result->partial=result->file.wstring()+L".partial";
        ULARGE_INTEGER free{};
        if(!GetDiskFreeSpaceExW(folder.c_str(),&free,nullptr,nullptr) || free.QuadPart<64*1024*1024)
            throw hresult_error(E_FAIL,L"At least 64 MB of free space is required to begin recording.");
        Graphics g(area,spec);
        try { encoder.open(result->partial,g,!forceSoftware,audio,initialAudio!=AudioSilent); opened=true; }
        catch(...) {
            // Only remove a file that this attempt actually created, never a collision owned by someone else.
            bool owned=bool(encoder.stream); encoder.close();
            if(owned) DeleteFileW(result->partial.c_str());
            if(forceSoftware || !owned) throw;
            encoder.open(result->partial,g,false,audio,initialAudio!=AudioSilent); opened=true;
        }
        result->gpuInput=encoder.gpu; result->qualityVbr=encoder.quality;
        std::vector<Monitor> monitors; EnumDisplayMonitors(nullptr,nullptr,enumerate,reinterpret_cast<LPARAM>(&monitors));
        std::vector<std::unique_ptr<Source>> sources;
        auto factory=get_activation_factory<GraphicsCaptureItem,IGraphicsCaptureItemInterop>();
        for(auto m:monitors) {
            auto intersection=intersect(area,m.bounds);
            if(intersection.width()<=0 || intersection.height()<=0) continue;
            auto src=std::make_unique<Source>(); src->monitor=m;
            check_hresult(factory->CreateForMonitor(m.handle,guid_of<GraphicsCaptureItem>(),put_abi(src->item)));
            auto size=src->item.Size();
            if(size.Width!=m.bounds.width() || size.Height!=m.bounds.height())
                throw hresult_error(E_FAIL,L"The monitor geometry changed. Select the region again.");
            auto closed=src->closed;
            src->closedToken=src->item.Closed([closed](auto const&,auto const&) { closed->store(true); });
            src->pool=Direct3D11CaptureFramePool::CreateFreeThreaded(g.winrtDevice,DirectXPixelFormat::B8G8R8A8UIntNormalized,2,size);
            src->session=src->pool.CreateCaptureSession(src->item);
            src->session.StartCapture(); sources.push_back(std::move(src));
        }
        if(sources.empty()) throw hresult_error(E_FAIL,L"The selection does not overlap a connected display.");
        const int64_t period=10000000/spec.fps;
        const int64_t readyDeadline=ticks()+50000000;
        std::unique_ptr<Timeline> timeline;
        int64_t next=0, spaceCheck=ticks(), audioFloor=0;
        bool wasPaused=false;
        while(true) {
            if(stop_.load() && (timeline || cancel_.load())) break;
            bool allReady=true;
            for(auto& src:sources) {
                if(src->closed->load()) throw hresult_error(E_FAIL,L"A captured display disconnected. The usable recording will be saved if possible.");
                MONITORINFO info{sizeof(info)};
                if(!GetMonitorInfoW(src->monitor.handle,&info) ||
                    info.rcMonitor.left!=src->monitor.bounds.left || info.rcMonitor.top!=src->monitor.bounds.top ||
                    info.rcMonitor.right!=src->monitor.bounds.right || info.rcMonitor.bottom!=src->monitor.bounds.bottom)
                    throw hresult_error(E_FAIL,L"Display layout changed. Start a new recording for the new layout.");
                // Drain only the two-frame pool; retaining the newest frame prevents a stale-frame queue.
                auto frame=src->pool.TryGetNextFrame();
                if(frame) {
                    auto newer=src->pool.TryGetNextFrame(); if(newer) { frame.Close(); frame=std::move(newer); }
                    auto size=frame.ContentSize();
                    if(size.Width!=src->monitor.bounds.width() || size.Height!=src->monitor.bounds.height())
                        throw hresult_error(E_FAIL,L"Display resolution changed. Start a new recording.");
                    auto access=frame.Surface().as<::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();
                    com_ptr<ID3D11Texture2D> texture;
                    check_hresult(access->GetInterface(__uuidof(ID3D11Texture2D),texture.put_void()));
                    Rect clip=intersect(area,src->monitor.bounds);
                    D3D11_BOX box{UINT(clip.left-src->monitor.bounds.left),UINT(clip.top-src->monitor.bounds.top),0,
                        UINT(clip.right-src->monitor.bounds.left),UINT(clip.bottom-src->monitor.bounds.top),1};
                    g.context->CopySubresourceRegion(g.canvas.get(),0,clip.left-area.left,clip.top-area.top,0,texture.get(),0,&box);
                    src->received=true; frame.Close();
                }
                allReady &= src->received;
            }
            auto now=ticks();
            if(!timeline) audio.collect(audioMode_.load(),nullptr,0,true);
            if(!allReady) {
                if(now>readyDeadline) throw hresult_error(E_FAIL,L"No screen frames arrived. Check capture permissions and try again.");
                std::unique_lock lock(mutex_); cv_.wait_for(lock,std::chrono::milliseconds(5)); continue;
            }
            if(!timeline) {
                now=ticks(); timeline=std::make_unique<Timeline>(now); next=0; audioFloor=now;
                result->startMs=double(now-requested)/10000.;
                PostMessageW(owner,WM_CAPTURE_STARTED,0,0);
            }
            bool paused=pause_.load();
            if(paused && !wasPaused) timeline->pause(now);
            if(!paused && wasPaused) { timeline->resume(now); audioFloor=now; }
            wasPaused=paused;
            int64_t time=timeline->at(now); elapsed_=time;
            audio.collect(audioMode_.load(),timeline.get(),audioFloor,paused,micGain_.load(),systemGain_.load());
            micLevel_=audio.micLevel(); systemLevel_=audio.systemLevel();
            // Audio is collected on dedicated workers, independent of this GPU /
            // H.264 loop. Leave a 300 ms PCM staging window for packet jitter.
            audio.writeUntil(encoder.writer.get(),std::max<int64_t>(0,time-3000000));
            if(!paused && time>=next) {
                if(time-next>period) result->lateFrames+=uint64_t((time-next)/period);
                if(pending) { encoder.write(pending.get(),time-pendingTime); duration=time; pending=nullptr; }
                auto texture=g.convert(); auto buffer=g.buffer(texture.get(),encoder.gpu);
                check_hresult(MFCreateSample(pending.put()));
                check_hresult(pending->AddBuffer(buffer.get())); check_hresult(pending->SetSampleTime(time));
                pendingTime=time; ++result->frames;
                next=time+period;
            }
            if(now-spaceCheck>10000000) {
                checked(g.device->GetDeviceRemovedReason(),L"The graphics device was lost. Start a new recording.");
                if(!GetDiskFreeSpaceExW(folder.c_str(),&free,nullptr,nullptr) || free.QuadPart<16*1024*1024)
                    throw hresult_error(E_FAIL,L"Recording stopped because free disk space is low.");
                spaceCheck=now;
            }
            std::unique_lock lock(mutex_);
            cv_.wait_for(lock,std::chrono::milliseconds(paused?25:std::clamp<int64_t>((next-time)/10000,1,15)));
        }
        result->cancelled=cancel_.load();
        if(!result->cancelled && pending) {
            auto end=std::max(pendingTime+period,timeline->at(ticks()));
            audio.collect(audioMode_.load(),timeline.get(),audioFloor,wasPaused,micGain_.load(),systemGain_.load());
            encoder.write(pending.get(),end-pendingTime); duration=end; pending=nullptr;
        }
    } catch(hresult_error const& e) {
        std::wstringstream message; message<<e.message().c_str()<<L" (0x"<<std::hex<<uint32_t(e.code())<<L")";
        result->error=message.str();
    } catch(std::exception const& e) {
        std::string message=e.what(); result->error=std::wstring(message.begin(),message.end());
    } catch(...) { result->error=L"An unexpected recording error occurred."; }
    result->cancelled=cancel_.load();
    // Finalize on the worker, including after a recoverable capture error.
    if(opened && !result->cancelled && result->frames) {
        try {
            if(pending) { encoder.write(pending.get(),10000000/spec.fps); duration=pendingTime+10000000/spec.fps; pending=nullptr; }
            audio.close(); // Flush both WASAPI workers before the final PCM/AAC write.
            audio.writeUntil(encoder.writer.get(),duration);
            encoder.finish();
            if(!MoveFileExW(result->partial.c_str(),result->file.c_str(),MOVEFILE_WRITE_THROUGH))
                throw hresult_error(HRESULT_FROM_WIN32(GetLastError()),L"The MP4 was finalized but could not be renamed. The .partial file is retained.");
            result->partial.clear();
            std::error_code ec; result->bytes=std::filesystem::file_size(result->file,ec);
            if(ec) result->bytes=0;
            result->seconds=double(duration)/10000000.;
        } catch(hresult_error const& e) { result->error+=L"\n"; result->error+=e.message().c_str(); result->file.clear(); }
        catch(...) { result->error+=L"\nFinalization failed; the partial file is retained."; result->file.clear(); }
    } else { encoder.close(); if(opened) DeleteFileW(result->partial.c_str()); result->file.clear(); }
    // Capture counters before endpoint teardown, even if the user toggled a source.
    const auto timestampCorrections=audio.timestampCorrections();
    const auto inferredTimestamps=audio.inferredTimestamps();
    pending=nullptr; audio.close(); encoder.close();
    if(mf) MFShutdown(); if(apartment) uninit_apartment();
    // A local sidecar records observed timing; no data leaves this machine.
    if(!result->file.empty()) {
        try {
            std::ofstream report(std::filesystem::path(result->file.wstring()+L".metrics.txt"));
            report<<"Glimpse 0.2.0\nwidth="<<spec.width<<"\nheight="<<spec.height<<"\nfps_limit="<<spec.fps
                <<"\nstart_latency_ms="<<result->startMs<<"\nduration_seconds="<<result->seconds
                <<"\nsubmitted_frames="<<result->frames<<"\nlate_frame_intervals="<<result->lateFrames
                <<"\nfile_bytes="<<result->bytes<<"\ngpu_input="<<result->gpuInput<<"\nquality_vbr_accepted="<<result->qualityVbr
                <<"\naudio_enabled="<<(initialAudio!=AudioSilent)<<"\naudio_dropped_frames="<<audio.dropped()<<"\naudio_discontinuities="<<audio.glitches()
                <<"\naudio_bad_timestamps="<<audio.badTimestamps()<<"\naudio_timestamp_corrections="<<timestampCorrections
                <<"\naudio_inferred_timestamps="<<inferredTimestamps<<"\naudio_mixed_overload_frames="<<audio.overloadedFrames()
                <<"\naudio_mixed_peak_before_limiter="<<audio.mixedPeak()
                <<"\nNote: GPU input does not prove that the selected encoder is hardware.\n";
        } catch(...) {}
    }
    auto ptr=result.release();
    if(!PostMessageW(owner,WM_CAPTURE_FINISHED,0,reinterpret_cast<LPARAM>(ptr))) delete ptr;
}
Recorder::~Recorder() { stop(true); join(); }
void Recorder::join() { if(worker_.joinable()) worker_.join(); }
void Recorder::start(HWND owner, Rect area, VideoSpec spec, const std::filesystem::path& folder, bool forceSoftware, unsigned audioMode) {
    join(); stop_=false; cancel_=false; pause_=false; elapsed_=0; audioMode_=audioMode; micLevel_=0; systemLevel_=0;
    worker_=std::thread(&Recorder::run,this,owner,area,spec,folder,forceSoftware,audioMode);
}
void Recorder::stop(bool discard) { if(discard) cancel_=true; stop_=true; cv_.notify_all(); }
void Recorder::pause(bool value) { pause_=value; cv_.notify_all(); }
}
