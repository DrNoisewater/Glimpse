#include "capture.hpp"
#include <windowsx.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <dwmapi.h>
#include <uxtheme.h>
#include <wtsapi32.h>
#include <winrt/base.h>
#include <algorithm>
#include <memory>
#include <vector>
#include <sstream>

using namespace glimpse;
namespace fs=std::filesystem;
namespace {
constexpr UINT TrayMessage=WM_APP+1, New=100, Recent=101, Folder=102, Settings=103, Exit=104;
constexpr UINT Record=200, Pause=201, Cancel=202, Copy=203, Reveal=204, Drag=205;
constexpr UINT Mic=206, SystemAudio=207, Dismiss=208, Indicator=209, MicVolume=210, SystemVolume=211;
constexpr UINT AudioMenuBase=400, ToastTimer=4;
constexpr UINT Save=300, Browse=301, RecentBase=1000;
constexpr UINT Hotkey=1, RetryCopy=2, ClockTimer=3;
enum class State { Idle, Selecting, Ready, Starting, Recording, Paused, Saving };
HINSTANCE instance;
HWND mainWindow{}, overlay{}, toolbar{}, settingsWindow{}, shareWindow{};
HWND recordingFrame{}, micSlider{}, systemSlider{};
HWND actionButton{}, pauseButton{}, elapsedLabel{}, shareText{};
HFONT uiFont{};
HBRUSH background{};
bool dark{}, quitAfterSave{}, dragging{}, clipboardPending{}, clipboardCopied{};
COLORREF bg{}, fg{};
UINT taskbarCreated{};
State state=State::Idle;
Rect area{}, desktop{};
POINT anchor{};
Recorder recorder;
NOTIFYICONDATAW tray{sizeof(tray)};
fs::path latestFile, configPath;
std::vector<fs::path> recentFiles;
std::wstring shareSummary;
int copyAttempts{};
unsigned liveAudio{};
bool audioArmed{}, sharingDrag{};
int liveMicVolume=100,liveSystemVolume=65;
ULONGLONG toastUntil{};
const wchar_t* fontFace=L"Segoe UI";
struct Config {
    fs::path folder;
    Preset preset=Preset::Share;
    int fps=0;
    WORD hotkey=MAKEWORD('R',HOTKEYF_CONTROL|HOTKEYF_SHIFT);
    bool startup=false, software=false;
    unsigned audio=AudioSilent;
    int micVolume=100,systemVolume=65;
} config;
int scale(HWND w,int n) { return MulDiv(n,w?GetDpiForWindow(w):96,96); }
HFONT panelFont(HWND w) {
    auto f=reinterpret_cast<HFONT>(GetPropW(w,L"GlimpseFont")); return f?f:uiFont;
}
void renewFont(HWND w,UINT dpi) {
    auto old=reinterpret_cast<HFONT>(GetPropW(w,L"GlimpseFont"));
    HFONT font=CreateFontW(-MulDiv(15,dpi,96),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,fontFace);
    SetPropW(w,L"GlimpseFont",font);
    for(HWND c=GetWindow(w,GW_CHILD);c;c=GetWindow(c,GW_HWNDNEXT)) SendMessageW(c,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);
    if(old) DeleteObject(old);
    auto oldHeading=reinterpret_cast<HFONT>(GetPropW(w,L"GlimpseHeading"));
    HFONT heading=CreateFontW(-MulDiv(18,dpi,96),0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,fontFace);
    SetPropW(w,L"GlimpseHeading",heading);
    if(auto label=GetDlgItem(w,214)) SendMessageW(label,WM_SETFONT,reinterpret_cast<WPARAM>(heading),TRUE);
    if(oldHeading) DeleteObject(oldHeading);
}
std::wstring windowText(HWND w) {
    int n=GetWindowTextLengthW(w); std::wstring s(size_t(n)+1,L'\0');
    GetWindowTextW(w,s.data(),n+1); s.resize(n); return s;
}
void error(const std::wstring& text, HWND parent=nullptr) {
    MessageBoxW(parent?parent:mainWindow,text.c_str(),L"Glimpse",MB_OK|MB_ICONERROR);
}
void themeWindow(HWND w) {
    BOOL value=dark; DwmSetWindowAttribute(w,20,&value,sizeof(value));
    DWORD corners=2; DwmSetWindowAttribute(w,33,&corners,sizeof(corners));
    MARGINS shadow{1,1,1,1}; DwmExtendFrameIntoClientArea(w,&shadow);
}
void exclude(HWND w) {
    if(!SetWindowDisplayAffinity(w,0x00000011))
        throw winrt::hresult_error(HRESULT_FROM_WIN32(GetLastError()),L"Windows could not exclude the recorder controls from capture.");
}
void balloon(const std::wstring& title,const std::wstring& body) {
    tray.uFlags=NIF_INFO; wcsncpy_s(tray.szInfoTitle,title.c_str(),_TRUNCATE);
    wcsncpy_s(tray.szInfo,body.c_str(),_TRUNCATE); tray.dwInfoFlags=NIIF_INFO;
    Shell_NotifyIconW(NIM_MODIFY,&tray);
}
void addTray() {
    tray.hWnd=mainWindow; tray.uID=1; tray.uFlags=NIF_MESSAGE|NIF_ICON|NIF_TIP;
    tray.uCallbackMessage=TrayMessage;
    tray.hIcon=LoadIconW(instance,MAKEINTRESOURCEW(1));
    wcscpy_s(tray.szTip,L"Glimpse — instant screen recording");
    Shell_NotifyIconW(NIM_ADD,&tray);
    tray.uVersion=NOTIFYICON_VERSION_4; Shell_NotifyIconW(NIM_SETVERSION,&tray);
}
fs::path knownFolder(REFKNOWNFOLDERID id) {
    PWSTR path=nullptr; winrt::check_hresult(SHGetKnownFolderPath(id,0,nullptr,&path));
    fs::path p(path); CoTaskMemFree(path); return p;
}
void readConfig() {
    configPath=knownFolder(FOLDERID_LocalAppData)/L"Glimpse"/L"settings.ini";
    config.folder=knownFolder(FOLDERID_Videos)/L"Glimpse";
    wchar_t path[32768]{};
    GetPrivateProfileStringW(L"Settings",L"Folder",config.folder.c_str(),path,DWORD(std::size(path)),configPath.c_str());
    config.folder=path;
    config.preset=Preset(std::clamp(int(GetPrivateProfileIntW(L"Settings",L"Preset",0,configPath.c_str())),0,2));
    int fps=int(GetPrivateProfileIntW(L"Settings",L"FPS",0,configPath.c_str()));
    config.fps=(fps==15||fps==30||fps==60)?fps:0;
    config.hotkey=WORD(GetPrivateProfileIntW(L"Settings",L"Hotkey",config.hotkey,configPath.c_str()));
    if(!LOBYTE(config.hotkey) || !(HIBYTE(config.hotkey)&(HOTKEYF_CONTROL|HOTKEYF_ALT)))
        config.hotkey=MAKEWORD('R',HOTKEYF_CONTROL|HOTKEYF_SHIFT);
    config.software=GetPrivateProfileIntW(L"Settings",L"Software",0,configPath.c_str())!=0;
    config.audio=GetPrivateProfileIntW(L"Settings",L"Audio",0,configPath.c_str())&AudioBoth;
    config.micVolume=std::clamp(int(GetPrivateProfileIntW(L"Settings",L"MicVolume",100,configPath.c_str())),0,100);
    config.systemVolume=std::clamp(int(GetPrivateProfileIntW(L"Settings",L"SystemVolume",65,configPath.c_str())),0,100);
    HKEY key{};
    if(RegOpenKeyExW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",0,KEY_QUERY_VALUE,&key)==ERROR_SUCCESS) {
        config.startup=RegQueryValueExW(key,L"Glimpse",nullptr,nullptr,nullptr,nullptr)==ERROR_SUCCESS; RegCloseKey(key);
    }
}
bool registerKey(WORD key) {
    BYTE flags=HIBYTE(key);
    UINT mods=MOD_NOREPEAT | ((flags&HOTKEYF_CONTROL)?MOD_CONTROL:0) |
        ((flags&HOTKEYF_SHIFT)?MOD_SHIFT:0) | ((flags&HOTKEYF_ALT)?MOD_ALT:0);
    return RegisterHotKey(mainWindow,Hotkey,mods,LOBYTE(key))!=FALSE;
}
void writeConfig(const Config& c) {
    fs::create_directories(configPath.parent_path()); auto tmp=configPath.wstring()+L".tmp";
    HANDLE file=CreateFileW(tmp.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE) winrt::throw_last_error();
    WORD bom=0xfeff; DWORD written=0; BOOL ok=WriteFile(file,&bom,sizeof(bom),&written,nullptr);
    CloseHandle(file);
    if(!ok||written!=sizeof(bom)) throw winrt::hresult_error(E_FAIL,L"Cannot write the settings file.");
    auto put=[&](const wchar_t* key,const std::wstring& value) {
        if(!WritePrivateProfileStringW(L"Settings",key,value.c_str(),tmp.c_str()))
            throw winrt::hresult_error(HRESULT_FROM_WIN32(GetLastError()),L"Cannot save settings.");
    };
    put(L"Folder",c.folder.wstring()); put(L"Preset",std::to_wstring(int(c.preset)));
    put(L"FPS",std::to_wstring(c.fps)); put(L"Hotkey",std::to_wstring(c.hotkey)); put(L"Software",c.software?L"1":L"0");
    put(L"Audio",std::to_wstring(c.audio)); put(L"MicVolume",std::to_wstring(c.micVolume)); put(L"SystemVolume",std::to_wstring(c.systemVolume));
    WritePrivateProfileStringW(nullptr,nullptr,nullptr,tmp.c_str());
    if(!MoveFileExW(tmp.c_str(),configPath.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))
        throw winrt::hresult_error(HRESULT_FROM_WIN32(GetLastError()),L"Cannot replace settings file.");
}
void setStartup(bool enabled) {
    HKEY key{};
    LONG status=RegCreateKeyExW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",0,nullptr,0,KEY_SET_VALUE,nullptr,&key,nullptr);
    if(status!=ERROR_SUCCESS) winrt::throw_hresult(HRESULT_FROM_WIN32(status));
    if(enabled) {
        wchar_t exe[32768]; GetModuleFileNameW(nullptr,exe,DWORD(std::size(exe)));
        std::wstring command=L"\""+std::wstring(exe)+L"\"";
        status=RegSetValueExW(key,L"Glimpse",0,REG_SZ,reinterpret_cast<const BYTE*>(command.c_str()),DWORD((command.size()+1)*sizeof(wchar_t)));
    } else { status=RegDeleteValueW(key,L"Glimpse"); if(status==ERROR_FILE_NOT_FOUND) status=ERROR_SUCCESS; }
    RegCloseKey(key); if(status!=ERROR_SUCCESS) winrt::throw_hresult(HRESULT_FROM_WIN32(status));
}
HWND child(HWND parent,const wchar_t* cls,const wchar_t* text,int id,int x,int y,int w,int h,DWORD extra=0) {
    HWND c=CreateWindowExW(0,cls,text,WS_CHILD|WS_VISIBLE|extra,scale(parent,x),scale(parent,y),scale(parent,w),scale(parent,h),parent,
        reinterpret_cast<HMENU>(INT_PTR(id)),instance,nullptr);
    HFONT font=id==214?reinterpret_cast<HFONT>(GetPropW(parent,L"GlimpseHeading")):panelFont(parent);
    SendMessageW(c,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE); return c;
}
HWND button(HWND p,const wchar_t* text,int id,int x,int y,int w) {
    return child(p,L"BUTTON",text,id,x,y,w,32,WS_TABSTOP|BS_OWNERDRAW);
}
void tip(HWND control,const wchar_t* text) {
    HWND t=CreateWindowExW(WS_EX_TOPMOST,TOOLTIPS_CLASSW,nullptr,WS_POPUP|TTS_ALWAYSTIP|TTS_NOPREFIX,
        CW_USEDEFAULT,CW_USEDEFAULT,CW_USEDEFAULT,CW_USEDEFAULT,GetParent(control),nullptr,instance,nullptr);
    if(!t) return;
    if(!SetWindowDisplayAffinity(t,0x00000011)) { DestroyWindow(t); return; }
    TOOLINFOW info{sizeof(info)}; info.uFlags=TTF_IDISHWND|TTF_SUBCLASS;
    info.hwnd=GetParent(control); info.uId=reinterpret_cast<UINT_PTR>(control); info.lpszText=const_cast<wchar_t*>(text);
    SendMessageW(t,TTM_ADDTOOLW,0,reinterpret_cast<LPARAM>(&info)); SendMessageW(t,TTM_SETMAXTIPWIDTH,0,300);
}
HWND slider(HWND parent,int id,int x,int y,int width,int volume,const wchar_t* name) {
    HWND s=child(parent,TRACKBAR_CLASSW,name,id,x,y,width,30,WS_TABSTOP|TBS_HORZ|TBS_NOTICKS);
    SendMessageW(s,TBM_SETRANGE,TRUE,MAKELPARAM(0,100)); SendMessageW(s,TBM_SETPOS,TRUE,volume);
    SendMessageW(s,TBM_SETPAGESIZE,0,10); tip(s,name); return s;
}
void audioControls() {
    if(!toolbar) return;
    SetDlgItemTextW(toolbar,Mic,(liveAudio&AudioMic)?L"Mic on":L"Mic off");
    SetDlgItemTextW(toolbar,SystemAudio,(liveAudio&AudioSystem)?L"Sound on":L"Sound off");
    SetDlgItemTextW(toolbar,212,(std::to_wstring(liveMicVolume)+L"%").c_str());
    SetDlgItemTextW(toolbar,213,(std::to_wstring(liveSystemVolume)+L"%").c_str());
    bool enable=audioArmed&&(state==State::Recording||state==State::Paused);
    for(UINT id:{Mic,SystemAudio,MicVolume,SystemVolume}) EnableWindow(GetDlgItem(toolbar,id),enable);
    recorder.audio(liveAudio); recorder.volume(liveMicVolume/100.f,liveSystemVolume/100.f);
}
void openFolder(const fs::path& file={}) {
    if(!file.empty()) {
        PIDLIST_ABSOLUTE pidl{};
        if(SUCCEEDED(SHParseDisplayName(file.c_str(),nullptr,&pidl,0,nullptr))) {
            SHOpenFolderAndSelectItems(pidl,0,nullptr,0); CoTaskMemFree(pidl); return;
        }
    }
    std::error_code ec; fs::create_directories(config.folder,ec);
    if(reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr,L"open",config.folder.c_str(),nullptr,nullptr,SW_SHOWNORMAL))<=32)
        error(L"The captures folder could not be opened.");
}
HGLOBAL fileDrop(const fs::path& file) {
    auto path=file.wstring(); size_t bytes=sizeof(DROPFILES)+(path.size()+2)*sizeof(wchar_t);
    HGLOBAL block=GlobalAlloc(GHND,bytes); if(!block) return nullptr;
    auto drop=static_cast<DROPFILES*>(GlobalLock(block)); if(!drop) { GlobalFree(block); return nullptr; }
    drop->pFiles=sizeof(DROPFILES); drop->fWide=TRUE;
    memcpy(reinterpret_cast<BYTE*>(drop)+sizeof(DROPFILES),path.c_str(),(path.size()+1)*sizeof(wchar_t));
    GlobalUnlock(block); return block;
}
bool copyFile() {
    std::error_code ec;
    if(latestFile.empty() || !fs::exists(latestFile,ec)) return false;
    HGLOBAL data=fileDrop(latestFile); if(!data) return false;
    if(!OpenClipboard(mainWindow)) { GlobalFree(data); return false; }
    bool ok=EmptyClipboard()!=FALSE && SetClipboardData(CF_HDROP,data)!=nullptr;
    CloseClipboard(); if(!ok) GlobalFree(data); return ok;
}
void startClipboardCopy() {
    KillTimer(mainWindow,RetryCopy);
    if(copyFile()) {
        clipboardPending=false; clipboardCopied=true;
        if(shareWindow) SetWindowTextW(shareText,(shareSummary+L"\nCopied — paste where file uploads are supported.").c_str());
    } else {
        copyAttempts=0; clipboardPending=true; clipboardCopied=false; SetTimer(mainWindow,RetryCopy,100,nullptr);
    }
}
class DropSource final:public IDropSource {
    LONG refs_=1;
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** p) override {
        if(!p) return E_POINTER; *p=nullptr;
        if(id==IID_IUnknown||id==IID_IDropSource) { *p=static_cast<IDropSource*>(this); AddRef(); return S_OK; } return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&refs_); }
    ULONG STDMETHODCALLTYPE Release() override { auto n=InterlockedDecrement(&refs_); if(!n) delete this; return n; }
    HRESULT STDMETHODCALLTYPE QueryContinueDrag(BOOL escape,DWORD keys) override {
        return escape?DRAGDROP_S_CANCEL:!(keys&MK_LBUTTON)?DRAGDROP_S_DROP:S_OK;
    }
    HRESULT STDMETHODCALLTYPE GiveFeedback(DWORD) override { return DRAGDROP_S_USEDEFAULTCURSORS; }
};
void dragFile() {
    std::error_code ec;
    if(latestFile.empty() || !fs::exists(latestFile,ec)) return;
    PIDLIST_ABSOLUTE full{};
    if(FAILED(SHParseDisplayName(latestFile.c_str(),nullptr,&full,0,nullptr))) return;
    auto parent=ILCloneFull(full);
    if(!parent) { CoTaskMemFree(full); return; }
    PCUITEMID_CHILD item=ILFindLastID(full); ILRemoveLastID(parent);
    winrt::com_ptr<IDataObject> data;
    HRESULT hr=SHCreateDataObject(parent,1,&item,nullptr,IID_IDataObject,data.put_void());
    if(SUCCEEDED(hr)) {
        winrt::com_ptr<IDropSource> source; source.attach(new DropSource()); DWORD effect=0;
        sharingDrag=true; DoDragDrop(data.get(),source.get(),DROPEFFECT_COPY,&effect); sharingDrag=false;
        toastUntil=GetTickCount64()+12000;
    }
    CoTaskMemFree(parent); CoTaskMemFree(full);
}
LRESULT CALLBACK dragProc(HWND w,UINT msg,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR) {
    if(msg==WM_LBUTTONDOWN) {
        POINT p{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)}; ClientToScreen(w,&p);
        if(DragDetect(w,p)) dragFile(); return 0;
    }
    if(msg==WM_NCDESTROY) RemoveWindowSubclass(w,dragProc,1);
    return DefSubclassProc(w,msg,wp,lp);
}
void closeSelection() {
    if(overlay) { DestroyWindow(overlay); overlay=nullptr; }
    if(toolbar) { DestroyWindow(toolbar); toolbar=nullptr; }
    if(recordingFrame) { DestroyWindow(recordingFrame); recordingFrame=nullptr; }
    KillTimer(mainWindow,ClockTimer); dragging=false; state=State::Idle;
}
void stopRecording(bool cancel=false) {
    if(state==State::Ready||state==State::Selecting) { closeSelection(); return; }
    if(state!=State::Recording&&state!=State::Paused&&state!=State::Starting) return;
    state=State::Saving; recorder.stop(cancel);
    audioControls();
    SetWindowTextW(elapsedLabel,cancel?L"Discarding…":L"Saving…");
    EnableWindow(actionButton,FALSE); EnableWindow(pauseButton,FALSE);
    EnableWindow(GetDlgItem(toolbar,Cancel),FALSE);
}
void makeFrame() {
    recordingFrame=CreateWindowExW(WS_EX_LAYERED|WS_EX_TRANSPARENT|WS_EX_NOACTIVATE|WS_EX_TOPMOST|WS_EX_TOOLWINDOW,
        L"GlimpseFrame",L"Recording boundary",WS_POPUP,area.left-3,area.top-3,area.width()+6,area.height()+6,
        mainWindow,nullptr,instance,nullptr);
    if(!recordingFrame) winrt::throw_last_error();
    int width=area.width()+6,height=area.height()+6;
    HRGN ring=CreateRectRgn(0,0,width,height),hole=CreateRectRgn(2,2,width-2,height-2);
    CombineRgn(ring,ring,hole,RGN_DIFF); DeleteObject(hole);
    if(width>32&&height>32) {
        HRGN dot=CreateEllipticRgn(width-23,9,width-9,23);
        CombineRgn(ring,ring,dot,RGN_OR); DeleteObject(dot);
    }
    if(!SetWindowRgn(recordingFrame,ring,FALSE)) DeleteObject(ring);
    SetLayeredWindowAttributes(recordingFrame,0,255,LWA_ALPHA); exclude(recordingFrame);
    ShowWindow(recordingFrame,SW_SHOWNOACTIVATE);
}
void startRecording() {
    if(state!=State::Ready) return;
    try {
        auto spec=videoSpec(area.width(),area.height(),config.preset,config.fps);
        if(overlay) { DestroyWindow(overlay); overlay=nullptr; }
        exclude(toolbar); makeFrame(); state=State::Starting;
        liveAudio=config.audio; audioArmed=liveAudio!=AudioSilent;
        liveMicVolume=config.micVolume; liveSystemVolume=config.systemVolume; audioControls();
        SetWindowTextW(actionButton,L"Stop"); EnableWindow(pauseButton,FALSE);
        SetWindowTextW(elapsedLabel,L"Starting…");
        recorder.start(mainWindow,area,spec,config.folder,config.software,liveAudio);
        SetTimer(mainWindow,ClockTimer,250,nullptr);
    } catch(winrt::hresult_error const& e) { closeSelection(); error(e.message().c_str()); }
    catch(std::exception const& e) { closeSelection(); std::string m=e.what(); error(std::wstring(m.begin(),m.end())); }
}
void positionPopup(HWND w,HMONITOR monitor,int preferredX,int preferredY,int width,int height) {
    MONITORINFO info{sizeof(info)}; GetMonitorInfoW(monitor,&info);
    int sw=scale(w,width), sh=scale(w,height);
    int x=std::clamp(preferredX,int(info.rcWork.left),std::max(int(info.rcWork.left),int(info.rcWork.right)-sw));
    int y=std::clamp(preferredY,int(info.rcWork.top),std::max(int(info.rcWork.top),int(info.rcWork.bottom)-sh));
    SetWindowPos(w,HWND_TOPMOST,x,y,sw,sh,SWP_NOACTIVATE);
}
void makeToolbar() {
    RECT selected{area.left,area.top,area.right,area.bottom};
    auto monitor=MonitorFromRect(&selected,MONITOR_DEFAULTTONEAREST);
    toolbar=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_TOPMOST,L"GlimpsePanel",L"Glimpse — recording controls",
        WS_POPUP|WS_BORDER,area.right-584,area.top,584,116,mainWindow,nullptr,instance,nullptr);
    if(!toolbar) winrt::throw_last_error();
    themeWindow(toolbar);
    int y=area.top-scale(toolbar,124);
    MONITORINFO mi{sizeof(mi)}; GetMonitorInfoW(monitor,&mi);
    if(y<mi.rcWork.top) y=area.top+scale(toolbar,8);
    positionPopup(toolbar,monitor,area.right-scale(toolbar,584),y,584,116);
    child(toolbar,L"STATIC",L"Recording status",Indicator,14,20,18,18,SS_OWNERDRAW);
    elapsedLabel=child(toolbar,L"STATIC",L"Starting…",0,44,18,220,26);
    pauseButton=button(toolbar,L"Pause",Pause,278,12,86); EnableWindow(pauseButton,FALSE);
    actionButton=button(toolbar,L"Stop",Record,372,12,92);
    auto discard=button(toolbar,L"Discard",Cancel,474,12,96); tip(discard,L"Discard this recording without saving it.");
    auto mic=button(toolbar,L"Mic off",Mic,14,68,80);
    micSlider=slider(toolbar,MicVolume,98,70,104,config.micVolume,L"Microphone volume. Use arrow keys to adjust.");
    child(toolbar,L"STATIC",L"",212,206,75,46,22);
    auto system=button(toolbar,L"Sound off",SystemAudio,258,68,92);
    systemSlider=slider(toolbar,SystemVolume,354,70,138,config.systemVolume,L"System audio volume. Lower this to make narration clearer.");
    child(toolbar,L"STATIC",L"",213,500,75,58,22);
    tip(mic,L"Mute/unmute microphone narration. The line below the slider shows the input level. Choose an audio mode before recording in the tray menu or Settings.");
    tip(system,L"Mute/unmute sound from the Windows default playback device. Choose an audio mode before recording.");
    ShowWindow(toolbar,SW_SHOWNOACTIVATE);
}
void updateHole() {
    if(!overlay) return;
    HRGN r=CreateRectRgn(0,0,desktop.width(),desktop.height());
    if(area.width()>2&&area.height()>2) {
        HRGN hole=CreateRectRgn(area.left-desktop.left+1,area.top-desktop.top+1,area.right-desktop.left-1,area.bottom-desktop.top-1);
        CombineRgn(r,r,hole,RGN_DIFF); DeleteObject(hole);
    }
    if(!SetWindowRgn(overlay,r,TRUE)) DeleteObject(r);
    InvalidateRect(overlay,nullptr,FALSE);
}
void newRecording() {
    if(state!=State::Idle) {
        if(state==State::Ready) startRecording();
        else if(state==State::Recording||state==State::Paused||state==State::Starting) stopRecording();
        return;
    }
    if(settingsWindow) { SetForegroundWindow(settingsWindow); return; }
    if(shareWindow) { DestroyWindow(shareWindow); shareWindow=nullptr; }
    desktop={GetSystemMetrics(SM_XVIRTUALSCREEN),GetSystemMetrics(SM_YVIRTUALSCREEN),0,0};
    desktop.right=desktop.left+GetSystemMetrics(SM_CXVIRTUALSCREEN); desktop.bottom=desktop.top+GetSystemMetrics(SM_CYVIRTUALSCREEN);
    area={}; state=State::Selecting;
    overlay=CreateWindowExW(WS_EX_TOPMOST|WS_EX_TOOLWINDOW|WS_EX_LAYERED,L"GlimpseOverlay",L"Drag to select · Escape to cancel",
        WS_POPUP,desktop.left,desktop.top,desktop.width(),desktop.height(),nullptr,nullptr,instance,nullptr);
    if(!overlay) { state=State::Idle; winrt::throw_last_error(); }
    SetLayeredWindowAttributes(overlay,0,112,LWA_ALPHA);
    ShowWindow(overlay,SW_SHOW); SetForegroundWindow(overlay); SetFocus(overlay);
}
void showShare() {
    if(latestFile.empty()) return;
    if(shareWindow) { SetForegroundWindow(shareWindow); return; }
    POINT point{}; GetCursorPos(&point); auto m=MonitorFromPoint(point,MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{sizeof(mi)}; GetMonitorInfoW(m,&mi);
    shareWindow=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_TOPMOST,L"GlimpsePanel",L"Glimpse — capture saved",
        WS_POPUP|WS_BORDER,point.x,point.y,488,224,mainWindow,nullptr,instance,nullptr);
    themeWindow(shareWindow);
    positionPopup(shareWindow,m,mi.rcWork.right-scale(shareWindow,504),mi.rcWork.bottom-scale(shareWindow,240),488,224);
    child(shareWindow,L"STATIC",L"✓  Capture saved",214,20,18,370,28);
    auto close=button(shareWindow,L"×",Dismiss,438,12,32); tip(close,L"Dismiss notification. Your file remains in Recent Captures.");
    auto message=shareSummary+(clipboardPending?L"\nCopying file…":clipboardCopied?L"\nCopied — paste where file uploads are supported.":L"\nCopy or drag this file to your destination app.");
    shareText=child(shareWindow,L"STATIC",message.c_str(),0,20,56,448,76);
    button(shareWindow,L"Copy file",Copy,20,144,124);
    button(shareWindow,L"Open folder",Reveal,156,144,136);
    auto drag=button(shareWindow,L"Drag to share",Drag,304,144,164); SetWindowSubclass(drag,dragProc,1,0);
    child(shareWindow,L"STATIC",L"Saved locally  ·  Ready to attach or share",0,20,192,448,22);
    ShowWindow(shareWindow,SW_SHOWNOACTIVATE);
    toastUntil=GetTickCount64()+12000; SetTimer(mainWindow,ToastTimer,500,nullptr);
}
void settings() {
    if(state!=State::Idle) return;
    if(settingsWindow) { SetForegroundWindow(settingsWindow); return; }
    settingsWindow=CreateWindowExW(WS_EX_TOOLWINDOW,L"GlimpsePanel",L"Glimpse settings",
        WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU,CW_USEDEFAULT,CW_USEDEFAULT,560,460,mainWindow,nullptr,instance,nullptr);
    themeWindow(settingsWindow);
    RECT r{0,0,scale(settingsWindow,544),scale(settingsWindow,456)};
    AdjustWindowRectExForDpi(&r,WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU,FALSE,WS_EX_TOOLWINDOW,GetDpiForWindow(settingsWindow));
    SetWindowPos(settingsWindow,nullptr,0,0,r.right-r.left,r.bottom-r.top,SWP_NOMOVE|SWP_NOZORDER);
    child(settingsWindow,L"STATIC",L"Capture folder",0,20,12,490,22);
    child(settingsWindow,L"EDIT",config.folder.c_str(),310,20,36,398,28,WS_BORDER|ES_AUTOHSCROLL|WS_TABSTOP);
    button(settingsWindow,L"Browse…",Browse,426,34,98);
    child(settingsWindow,L"STATIC",L"Recording shortcut",0,20,78,210,22);
    auto hotkey=child(settingsWindow,HOTKEY_CLASSW,L"",311,244,74,280,28,WS_BORDER|WS_TABSTOP);
    SendMessageW(hotkey,HKM_SETHOTKEY,config.hotkey,0);
    child(settingsWindow,L"STATIC",L"Quality",0,20,112,200,22);
    auto presets=child(settingsWindow,L"COMBOBOX",L"",312,244,108,280,160,CBS_DROPDOWNLIST|WS_TABSTOP|WS_VSCROLL);
    for(auto s:{L"AI Share — 1080p, up to 30 FPS",L"High Quality — 4K, up to 60 FPS",L"Smallest File — 720p, 15 FPS"}) SendMessageW(presets,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(s));
    SendMessageW(presets,CB_SETCURSEL,int(config.preset),0);
    child(settingsWindow,L"STATIC",L"Frame rate limit",0,20,146,200,22);
    auto fps=child(settingsWindow,L"COMBOBOX",L"",313,244,142,280,140,CBS_DROPDOWNLIST|WS_TABSTOP|WS_VSCROLL);
    for(auto s:{L"Preset default",L"15 FPS",L"30 FPS",L"60 FPS (High Quality)"}) SendMessageW(fps,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(s));
    SendMessageW(fps,CB_SETCURSEL,config.fps==15?1:config.fps==30?2:config.fps==60?3:0,0);
    child(settingsWindow,L"STATIC",L"Audio sources",0,20,180,200,22);
    auto audio=child(settingsWindow,L"COMBOBOX",L"",316,244,176,280,160,CBS_DROPDOWNLIST|WS_TABSTOP|WS_VSCROLL);
    for(auto s:{L"Silent",L"Microphone narration",L"System audio",L"Microphone + system audio"}) SendMessageW(audio,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(s));
    SendMessageW(audio,CB_SETCURSEL,config.audio,0);
    child(settingsWindow,L"STATIC",L"Microphone volume",0,20,220,210,22);
    slider(settingsWindow,317,244,214,220,config.micVolume,L"Default microphone volume");
    child(settingsWindow,L"STATIC",(std::to_wstring(config.micVolume)+L"%").c_str(),319,474,220,50,22);
    child(settingsWindow,L"STATIC",L"System audio volume",0,20,256,210,22);
    slider(settingsWindow,318,244,250,220,config.systemVolume,L"Default system audio volume");
    child(settingsWindow,L"STATIC",(std::to_wstring(config.systemVolume)+L"%").c_str(),320,474,256,50,22);
    auto startup=child(settingsWindow,L"BUTTON",L"Start in the tray when I sign in",314,20,296,490,26,BS_AUTOCHECKBOX|WS_TABSTOP);
    if(dark) SetWindowTheme(startup,L"",L"");
    SendMessageW(startup,BM_SETCHECK,config.startup?BST_CHECKED:BST_UNCHECKED,0);
    auto software=child(settingsWindow,L"BUTTON",L"Use software encoding (compatibility mode)",315,20,328,490,26,BS_AUTOCHECKBOX|WS_TABSTOP);
    if(dark) SetWindowTheme(software,L"",L"");
    SendMessageW(software,BM_SETCHECK,config.software?BST_CHECKED:BST_UNCHECKED,0);
    child(settingsWindow,L"STATIC",L"Drag, release, record. Uses your Windows default audio devices.\nKeep system audio lower for clearer microphone narration.",0,20,366,504,38);
    button(settingsWindow,L"Save settings",Save,300,412,124); button(settingsWindow,L"Cancel",Cancel,436,412,88);
    ShowWindow(settingsWindow,SW_SHOWNORMAL); SetForegroundWindow(settingsWindow);
}
void saveSettings() {
    Config next=config;
    next.folder=windowText(GetDlgItem(settingsWindow,310));
    next.hotkey=WORD(SendDlgItemMessageW(settingsWindow,311,HKM_GETHOTKEY,0,0));
    next.preset=Preset(SendDlgItemMessageW(settingsWindow,312,CB_GETCURSEL,0,0));
    int selection=int(SendDlgItemMessageW(settingsWindow,313,CB_GETCURSEL,0,0));
    next.fps=selection==1?15:selection==2?30:selection==3?60:0;
    next.startup=SendDlgItemMessageW(settingsWindow,314,BM_GETCHECK,0,0)==BST_CHECKED;
    next.software=SendDlgItemMessageW(settingsWindow,315,BM_GETCHECK,0,0)==BST_CHECKED;
    next.audio=unsigned(SendDlgItemMessageW(settingsWindow,316,CB_GETCURSEL,0,0));
    next.micVolume=int(SendDlgItemMessageW(settingsWindow,317,TBM_GETPOS,0,0));
    next.systemVolume=int(SendDlgItemMessageW(settingsWindow,318,TBM_GETPOS,0,0));
    if(next.folder.empty() || !next.folder.is_absolute()) { error(L"Choose an absolute capture folder path.",settingsWindow); return; }
    if(!LOBYTE(next.hotkey)||!(HIBYTE(next.hotkey)&(HOTKEYF_CONTROL|HOTKEYF_ALT))) {
        error(L"Choose a shortcut containing Ctrl or Alt and a key.",settingsWindow); return;
    }
    bool changedKey=next.hotkey!=config.hotkey;
    if(changedKey) {
        UnregisterHotKey(mainWindow,Hotkey);
        if(!registerKey(next.hotkey)) { registerKey(config.hotkey); error(L"That shortcut is in use. Choose a different one.",settingsWindow); return; }
    }
    try {
        fs::create_directories(next.folder);
        setStartup(next.startup); writeConfig(next); config=next;
        DestroyWindow(settingsWindow); settingsWindow=nullptr;
    } catch(winrt::hresult_error const& e) {
        if(changedKey) { UnregisterHotKey(mainWindow,Hotkey); registerKey(config.hotkey); }
        try { setStartup(config.startup); } catch(...) {}
        error(e.message().c_str(),settingsWindow);
    } catch(std::exception const&) {
        if(changedKey) { UnregisterHotKey(mainWindow,Hotkey); registerKey(config.hotkey); }
        error(L"Could not create the capture folder or save settings. Check permissions.",settingsWindow);
    }
}
void browseFolder() {
    winrt::com_ptr<IFileOpenDialog> dialog;
    if(FAILED(CoCreateInstance(CLSID_FileOpenDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(dialog.put())))) return;
    DWORD flags=0; dialog->GetOptions(&flags); dialog->SetOptions(flags|FOS_PICKFOLDERS|FOS_FORCEFILESYSTEM);
    if(SUCCEEDED(dialog->Show(settingsWindow))) {
        winrt::com_ptr<IShellItem> item; dialog->GetResult(item.put()); PWSTR path=nullptr;
        if(SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH,&path))) { SetDlgItemTextW(settingsWindow,310,path); CoTaskMemFree(path); }
    }
}
void trayMenu() {
    HMENU menu=CreatePopupMenu(), recent=CreatePopupMenu(); recentFiles.clear();
    std::error_code ec;
    std::vector<std::pair<fs::file_time_type,fs::path>> candidates;
    for(fs::directory_iterator i(config.folder,ec),end; !ec && i!=end; i.increment(ec)) {
        if(i->path().extension()==L".mp4") { auto t=i->last_write_time(ec); if(!ec) candidates.emplace_back(t,i->path()); }
    }
    std::sort(candidates.begin(),candidates.end(),[](auto& a,auto& b){return a.first>b.first;});
    for(size_t i=0;i<std::min<size_t>(10,candidates.size());++i) {
        recentFiles.push_back(candidates[i].second);
        AppendMenuW(recent,MF_STRING,RecentBase+i,recentFiles.back().filename().c_str());
    }
    if(recentFiles.empty()) AppendMenuW(recent,MF_GRAYED,0,L"No captures yet");
    AppendMenuW(menu,MF_STRING|(state==State::Idle?0:MF_GRAYED),New,L"New Recording");
    HMENU audio=CreatePopupMenu();
    const wchar_t* modes[]{L"Silent",L"Microphone narration",L"System audio",L"Microphone + system audio"};
    for(unsigned i=0;i<4;++i) AppendMenuW(audio,MF_STRING|(config.audio==i?MF_CHECKED:0),AudioMenuBase+i,modes[i]);
    AppendMenuW(menu,MF_POPUP|(state==State::Idle?0:MF_GRAYED),reinterpret_cast<UINT_PTR>(audio),L"Audio for next recording");
    if(state==State::Recording||state==State::Paused||state==State::Starting) AppendMenuW(menu,MF_STRING,Record,L"Stop and save");
    AppendMenuW(menu,MF_POPUP,reinterpret_cast<UINT_PTR>(recent),L"Recent Captures");
    AppendMenuW(menu,MF_STRING,Folder,L"Open Captures Folder");
    AppendMenuW(menu,MF_STRING|(state==State::Idle?0:MF_GRAYED),Settings,L"Settings");
    AppendMenuW(menu,MF_SEPARATOR,0,nullptr); AppendMenuW(menu,MF_STRING,Exit,L"Exit");
    POINT p; GetCursorPos(&p); SetForegroundWindow(mainWindow);
    UINT cmd=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,p.x,p.y,0,mainWindow,nullptr);
    DestroyMenu(menu); PostMessageW(mainWindow,WM_NULL,0,0);
    if(cmd) PostMessageW(mainWindow,WM_COMMAND,cmd,0);
}
LRESULT CALLBACK overlayProc(HWND w,UINT msg,WPARAM wp,LPARAM lp) {
    switch(msg) {
    case WM_SETCURSOR: SetCursor(LoadCursorW(nullptr,IDC_CROSS)); return TRUE;
    case WM_LBUTTONDOWN:
        if(state!=State::Selecting) return 0;
        GetCursorPos(&anchor); dragging=true; SetCapture(w); return 0;
    case WM_MOUSEMOVE:
        if(dragging) { POINT p; GetCursorPos(&p); area=intersect(normalize(anchor.x,anchor.y,p.x,p.y),desktop); updateHole(); }
        return 0;
    case WM_LBUTTONUP:
        if(dragging) {
            POINT p; GetCursorPos(&p); area=intersect(normalize(anchor.x,anchor.y,p.x,p.y),desktop);
            dragging=false; ReleaseCapture();
            if(area.width()<2||area.height()<2) { area={}; updateHole(); return 0; }
            updateHole(); state=State::Ready; makeToolbar(); startRecording();
        } return 0;
    case WM_KEYDOWN: if(wp==VK_ESCAPE) closeSelection(); else if(wp==VK_RETURN) startRecording(); return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC dc=BeginPaint(w,&ps); RECT r; GetClientRect(w,&r);
        FillRect(dc,&r,static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
        SetBkMode(dc,TRANSPARENT); SetTextColor(dc,RGB(255,255,255)); SelectObject(dc,uiFont);
        POINT p; GetCursorPos(&p); auto mon=MonitorFromPoint(p,MONITOR_DEFAULTTONEAREST); MONITORINFO mi{sizeof(mi)}; GetMonitorInfoW(mon,&mi);
        RECT text{mi.rcWork.left-desktop.left+24,mi.rcWork.top-desktop.top+24,mi.rcWork.right-desktop.left-24,mi.rcWork.top-desktop.top+80};
        DrawTextW(dc,L"Drag a box. Release to record.  ·  Esc to cancel",-1,&text,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        if(dragging&&area.width()>2&&area.height()>2) {
            std::wstring dimensions=std::to_wstring(area.width())+L" × "+std::to_wstring(area.height());
            RECT label{area.left-desktop.left,area.bottom-desktop.top+10,area.left-desktop.left+220,area.bottom-desktop.top+44};
            DrawTextW(dc,dimensions.c_str(),-1,&label,DT_LEFT|DT_SINGLELINE);
        }
        EndPaint(w,&ps); return 0;
    }
    case WM_DISPLAYCHANGE: closeSelection(); return 0;
    }
    return DefWindowProcW(w,msg,wp,lp);
}
void handleCommand(HWND w,UINT id) {
    if(id>=AudioMenuBase&&id<AudioMenuBase+4&&state==State::Idle) {
        Config next=config; next.audio=id-AudioMenuBase; writeConfig(next); config=next; return;
    }
    if(id>=RecentBase&&id<RecentBase+recentFiles.size()) {
        KillTimer(mainWindow,RetryCopy); clipboardPending=false; clipboardCopied=false;
        latestFile=recentFiles[id-RecentBase]; shareSummary=latestFile.filename().wstring()+L"\nSaved capture — copy, reveal, or drag the file.";
        if(shareWindow) { DestroyWindow(shareWindow); shareWindow=nullptr; } showShare(); return;
    }
    switch(id) {
    case New: newRecording(); break;
    case Folder: openFolder(); break;
    case Settings: settings(); break;
    case Exit:
        if(state==State::Saving) quitAfterSave=true;
        else if(state==State::Recording||state==State::Paused||state==State::Starting) { quitAfterSave=true; stopRecording(); }
        else { closeSelection(); DestroyWindow(mainWindow); } break;
    case Record: if(state==State::Ready) startRecording(); else stopRecording(); break;
    case Mic: case SystemAudio:
        if(audioArmed&&(state==State::Recording||state==State::Paused)) {
            liveAudio^=id==Mic?AudioMic:AudioSystem; audioControls();
        } break;
    case Dismiss: if(shareWindow) { DestroyWindow(shareWindow); shareWindow=nullptr; } break;
    case Pause:
        if(state==State::Recording||state==State::Paused) {
            bool p=state==State::Recording; state=p?State::Paused:State::Recording;
            recorder.pause(p); SetWindowTextW(pauseButton,p?L"Resume":L"Pause");
            InvalidateRect(recordingFrame,nullptr,FALSE);
        } break;
    case Cancel:
        if(w==settingsWindow) { DestroyWindow(w); settingsWindow=nullptr; }
        else stopRecording(true); break;
    case Copy: startClipboardCopy(); break;
    case Reveal: openFolder(latestFile); break;
    case Save: saveSettings(); break;
    case Browse: browseFolder(); break;
    }
}
LRESULT CALLBACK panelProc(HWND w,UINT msg,WPARAM wp,LPARAM lp) {
    switch(msg) {
    case WM_COMMAND: handleCommand(w,LOWORD(wp)); return 0;
    case WM_NOTIFY: {
        auto header=reinterpret_cast<NMHDR*>(lp);
        if(header->code!=NM_CUSTOMDRAW) break;
        auto id=header->idFrom;
        if(id!=MicVolume&&id!=SystemVolume&&id!=317&&id!=318) break;
        auto draw=reinterpret_cast<NMCUSTOMDRAW*>(lp);
        if(draw->dwDrawStage==CDDS_PREPAINT) return CDRF_NOTIFYITEMDRAW;
        if(draw->dwDrawStage!=CDDS_ITEMPREPAINT) break;
        RECT r=draw->rc; bool enabled=IsWindowEnabled(header->hwndFrom)!=FALSE;
        if(draw->dwItemSpec==TBCD_CHANNEL) {
            int middle=(r.top+r.bottom)/2; r.top=middle-scale(w,2); r.bottom=middle+scale(w,2);
            auto base=CreateSolidBrush(dark?RGB(76,74,87):RGB(211,209,224)); FillRect(draw->hdc,&r,base); DeleteObject(base);
            int percent=int(SendMessageW(header->hwndFrom,TBM_GETPOS,0,0)); r.right=r.left+MulDiv(r.right-r.left,percent,100);
            auto fill=CreateSolidBrush(enabled?RGB(150,119,240):RGB(130,130,138)); FillRect(draw->hdc,&r,fill); DeleteObject(fill);
            return CDRF_SKIPDEFAULT;
        }
        if(draw->dwItemSpec==TBCD_THUMB) {
            int cx=(r.left+r.right)/2,cy=(r.top+r.bottom)/2,radius=scale(w,6);
            auto fill=CreateSolidBrush(enabled?(dark?RGB(226,215,255):RGB(112,82,219)):RGB(130,130,138));
            auto oldBrush=SelectObject(draw->hdc,fill); auto oldPen=SelectObject(draw->hdc,GetStockObject(NULL_PEN));
            Ellipse(draw->hdc,cx-radius,cy-radius,cx+radius,cy+radius);
            SelectObject(draw->hdc,oldPen); SelectObject(draw->hdc,oldBrush); DeleteObject(fill);
            if(GetFocus()==header->hwndFrom) DrawFocusRect(draw->hdc,&r);
            return CDRF_SKIPDEFAULT;
        }
        break;
    }
    case WM_HSCROLL:
        if(w==toolbar) {
            liveMicVolume=int(SendMessageW(micSlider,TBM_GETPOS,0,0));
            liveSystemVolume=int(SendMessageW(systemSlider,TBM_GETPOS,0,0)); audioControls();
        } else if(w==settingsWindow) {
            for(auto pair:{std::pair{317,319},std::pair{318,320}}) {
                auto value=SendDlgItemMessageW(w,pair.first,TBM_GETPOS,0,0);
                SetDlgItemTextW(w,pair.second,(std::to_wstring(value)+L"%").c_str());
            }
        } return 0;
    case WM_CLOSE:
        if(w==toolbar) stopRecording();
        else { if(w==settingsWindow) settingsWindow=nullptr; if(w==shareWindow) shareWindow=nullptr; DestroyWindow(w); }
        return 0;
    case WM_NCHITTEST: {
        auto hit=DefWindowProcW(w,msg,wp,lp);
        if(hit==HTCLIENT && w!=settingsWindow) return HTCAPTION; return hit;
    }
    case WM_DPICHANGED: {
        auto r=reinterpret_cast<RECT*>(lp);
        SetWindowPos(w,nullptr,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER|SWP_NOACTIVATE);
        UINT oldDpi=UINT(reinterpret_cast<UINT_PTR>(GetPropW(w,L"GlimpseDpi"))); if(!oldDpi) oldDpi=96;
        UINT newDpi=HIWORD(wp);
        for(HWND c=GetWindow(w,GW_CHILD);c;c=GetWindow(c,GW_HWNDNEXT)) {
            RECT cr; GetWindowRect(c,&cr); MapWindowPoints(nullptr,w,reinterpret_cast<POINT*>(&cr),2);
            SetWindowPos(c,nullptr,MulDiv(cr.left,newDpi,oldDpi),MulDiv(cr.top,newDpi,oldDpi),MulDiv(cr.right-cr.left,newDpi,oldDpi),MulDiv(cr.bottom-cr.top,newDpi,oldDpi),SWP_NOZORDER);
        }
        renewFont(w,newDpi);
        SetPropW(w,L"GlimpseDpi",reinterpret_cast<HANDLE>(UINT_PTR(newDpi))); return 0;
    }
    case WM_CREATE:
        SetPropW(w,L"GlimpseDpi",reinterpret_cast<HANDLE>(UINT_PTR(GetDpiForWindow(w))));
        renewFont(w,GetDpiForWindow(w)); return 0;
    case WM_NCDESTROY:
        if(auto f=RemovePropW(w,L"GlimpseFont")) DeleteObject(f);
        if(auto f=RemovePropW(w,L"GlimpseHeading")) DeleteObject(f);
        RemovePropW(w,L"GlimpseDpi"); break;
    case WM_SIZE:
        if((GetWindowLongPtrW(w,GWL_STYLE)&WS_CAPTION)!=WS_CAPTION) {
            RECT r; GetWindowRect(w,&r);
            HRGN region=CreateRoundRectRgn(0,0,r.right-r.left+1,r.bottom-r.top+1,scale(w,18),scale(w,18));
            if(!SetWindowRgn(w,region,TRUE)) DeleteObject(region);
        } return 0;
    case WM_CTLCOLORSTATIC: case WM_CTLCOLORBTN: {
        HDC dc=reinterpret_cast<HDC>(wp); SetTextColor(dc,fg); SetBkColor(dc,bg); return reinterpret_cast<LRESULT>(background);
    }
    case WM_ERASEBKGND: { RECT r; GetClientRect(w,&r); FillRect(reinterpret_cast<HDC>(wp),&r,background); return 1; }
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC dc=BeginPaint(w,&ps); RECT r; GetClientRect(w,&r); FillRect(dc,&r,background);
        if(w!=settingsWindow) {
            HPEN pen=CreatePen(PS_SOLID,1,dark?RGB(65,65,74):RGB(219,220,228));
            auto oldPen=SelectObject(dc,pen); auto oldBrush=SelectObject(dc,GetStockObject(NULL_BRUSH));
            RoundRect(dc,0,0,r.right,r.bottom,scale(w,18),scale(w,18));
            if(w==toolbar) { MoveToEx(dc,scale(w,14),scale(w,56),nullptr); LineTo(dc,r.right-scale(w,14),scale(w,56)); }
            SelectObject(dc,oldBrush); SelectObject(dc,oldPen); DeleteObject(pen);
            if(w==toolbar) {
                for(int source=0;source<2;++source) {
                    int x=source?354:98,width=source?138:104;
                    float level=state==State::Recording?(source?recorder.systemLevel():recorder.micLevel()):0;
                    RECT meter{scale(w,x),scale(w,104),scale(w,x+width),scale(w,107)};
                    auto base=CreateSolidBrush(dark?RGB(57,57,65):RGB(218,218,227)); FillRect(dc,&meter,base); DeleteObject(base);
                    meter.right=meter.left+int(scale(w,width)*std::clamp(level,0.f,1.f));
                    auto active=CreateSolidBrush(level>.95f?RGB(242,93,72):RGB(156,129,245)); FillRect(dc,&meter,active); DeleteObject(active);
                }
            }
        }
        EndPaint(w,&ps); return 0;
    }
    case WM_DRAWITEM: {
        auto d=reinterpret_cast<DRAWITEMSTRUCT*>(lp);
        if(d->CtlID==Indicator) {
            FillRect(d->hDC,&d->rcItem,background);
            bool on=state==State::Recording&&(GetTickCount64()%1000<500);
            COLORREF color=state==State::Paused?RGB(239,174,64):state==State::Recording?(on?RGB(248,76,84):RGB(114,42,47)):RGB(151,132,238);
            auto brush=CreateSolidBrush(color); auto old=SelectObject(d->hDC,brush); auto pen=SelectObject(d->hDC,GetStockObject(NULL_PEN));
            Ellipse(d->hDC,2,2,d->rcItem.right-2,d->rcItem.bottom-2);
            SelectObject(d->hDC,pen); SelectObject(d->hDC,old); DeleteObject(brush); return TRUE;
        }
        bool disabled=(d->itemState&ODS_DISABLED)!=0, pressed=(d->itemState&ODS_SELECTED)!=0;
        COLORREF color=pressed?(dark?RGB(70,70,75):RGB(206,208,214)):(dark?RGB(49,49,54):RGB(230,231,235));
        bool accent=d->CtlID==Copy||d->CtlID==Save, stop=d->CtlID==Record;
        bool active=(d->CtlID==Mic&&(liveAudio&AudioMic))||(d->CtlID==SystemAudio&&(liveAudio&AudioSystem));
        if(!disabled) {
            if(accent) color=pressed?RGB(90,65,190):RGB(112,82,219);
            if(stop) color=pressed?RGB(165,37,51):RGB(209,49,65);
            if(active) color=dark?RGB(64,52,93):RGB(228,218,253);
        }
        FillRect(d->hDC,&d->rcItem,background);
        auto brush=CreateSolidBrush(color); auto oldBrush=SelectObject(d->hDC,brush); auto oldPen=SelectObject(d->hDC,GetStockObject(NULL_PEN));
        RoundRect(d->hDC,d->rcItem.left,d->rcItem.top,d->rcItem.right,d->rcItem.bottom,scale(w,10),scale(w,10));
        SelectObject(d->hDC,oldPen); SelectObject(d->hDC,oldBrush); DeleteObject(brush);
        SetBkMode(d->hDC,TRANSPARENT); SetTextColor(d->hDC,disabled?RGB(128,128,132):(accent||stop)?RGB(255,255,255):fg); SelectObject(d->hDC,panelFont(w));
        auto text=windowText(d->hwndItem); RECT r=d->rcItem;
        DrawTextW(d->hDC,text.c_str(),-1,&r,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        if(d->itemState&ODS_FOCUS) { InflateRect(&r,-3,-3); DrawFocusRect(d->hDC,&r); } return TRUE;
    }
    }
    return DefWindowProcW(w,msg,wp,lp);
}
LRESULT CALLBACK frameProc(HWND w,UINT msg,WPARAM wp,LPARAM lp) {
    if(msg==WM_NCHITTEST) return HTTRANSPARENT;
    if(msg==WM_ERASEBKGND) return 1;
    if(msg==WM_PAINT) {
        PAINTSTRUCT ps; HDC dc=BeginPaint(w,&ps); RECT r; GetClientRect(w,&r);
        COLORREF color=state==State::Paused?RGB(239,174,64):state==State::Recording?RGB(244,79,88):RGB(151,132,238);
        HBRUSH brush=CreateSolidBrush(color); FillRect(dc,&r,brush); DeleteObject(brush);
        if(state==State::Recording&&GetTickCount64()%1000>=500) {
            RECT dot{r.right-23,9,r.right-9,23}; brush=CreateSolidBrush(RGB(88,33,39)); FillRect(dc,&dot,brush); DeleteObject(brush);
        }
        EndPaint(w,&ps); return 0;
    }
    return DefWindowProcW(w,msg,wp,lp);
}
LRESULT CALLBACK mainProc(HWND w,UINT msg,WPARAM wp,LPARAM lp) {
    if(msg==taskbarCreated) { addTray(); return 0; }
    switch(msg) {
    case WM_HOTKEY: newRecording(); return 0;
    case TrayMessage:
        if(LOWORD(lp)==WM_CONTEXTMENU||LOWORD(lp)==WM_RBUTTONUP) trayMenu();
        else if(LOWORD(lp)==NIN_SELECT||LOWORD(lp)==NIN_KEYSELECT) newRecording();
        else if(LOWORD(lp)==NIN_BALLOONUSERCLICK) showShare();
        return 0;
    case WM_COMMAND: handleCommand(w,LOWORD(wp)); return 0;
    case WM_CAPTURE_STARTED:
        if(state==State::Starting) { state=State::Recording; EnableWindow(pauseButton,TRUE); SetWindowTextW(elapsedLabel,L"REC  00:00"); audioControls(); }
        return 0;
    case WM_CAPTURE_FINISHED: {
        std::unique_ptr<CaptureResult> result(reinterpret_cast<CaptureResult*>(lp)); recorder.join(); closeSelection();
        if(!result->file.empty()) {
            latestFile=result->file;
            wchar_t detail[128]; swprintf_s(detail,L"%.1f MB  ·  %.1f seconds  ·  %s",double(result->bytes)/1048576.,result->seconds,audioArmed?L"Audio included":L"Silent");
            shareSummary=latestFile.filename().wstring()+L"\n"+detail;
            startClipboardCopy(); showShare();
        }
        if(!result->error.empty()) {
            std::wstring message=result->error;
            if(!result->partial.empty() && fs::exists(result->partial)) message+=L"\n\nPartial file retained at:\n"+result->partial.wstring();
            if(!result->file.empty()) message+=L"\n\nThe usable portion was saved.";
            error(message);
        }
        if(quitAfterSave) DestroyWindow(mainWindow); return 0;
    }
    case WM_TIMER:
        if(wp==ToastTimer) {
            if(!shareWindow) { KillTimer(w,ToastTimer); return 0; }
            POINT p; GetCursorPos(&p); RECT r; GetWindowRect(shareWindow,&r);
            if(PtInRect(&r,p)||GetForegroundWindow()==shareWindow||sharingDrag||clipboardPending) toastUntil=GetTickCount64()+12000;
            if(GetTickCount64()>=toastUntil) { DestroyWindow(shareWindow); shareWindow=nullptr; KillTimer(w,ToastTimer); }
        } else if(wp==ClockTimer) {
            if(recordingFrame) InvalidateRect(recordingFrame,nullptr,FALSE);
            if(toolbar) InvalidateRect(GetDlgItem(toolbar,Indicator),nullptr,FALSE);
            if(toolbar) { RECT meters{scale(toolbar,98),scale(toolbar,104),scale(toolbar,492),scale(toolbar,108)}; InvalidateRect(toolbar,&meters,FALSE); }
            if(state!=State::Recording&&state!=State::Paused) return 0;
            int seconds=int(recorder.elapsed()); wchar_t text[80];
            swprintf_s(text,L"%s  %02d:%02d%s",state==State::Paused?L"PAUSED":L"REC",seconds/60,seconds%60,audioArmed?L"":L"  ·  silent");
            SetWindowTextW(elapsedLabel,text);
        } else if(wp==RetryCopy) {
            bool copied=copyFile();
            if(copied||++copyAttempts>=20) {
                KillTimer(w,RetryCopy); clipboardPending=false; clipboardCopied=copied;
                auto summary=shareSummary+(copied?L"\nCopied — paste where file uploads are supported.":L"\nClipboard busy — use Copy file or Drag to share.");
                if(shareWindow) SetWindowTextW(shareText,summary.c_str());
                if(!copied) balloon(L"Capture saved; clipboard busy",L"Use Copy file again or drag the video from the share panel.");
            }
        } return 0;
    case WM_QUERYENDSESSION: recorder.stop(); return TRUE;
    case WM_WTSSESSION_CHANGE:
        if(wp==WTS_SESSION_LOCK||wp==WTS_REMOTE_DISCONNECT||wp==WTS_CONSOLE_DISCONNECT) stopRecording();
        return 0;
    case WM_ENDSESSION: if(wp) { recorder.stop(); recorder.join(); } return 0;
    case WM_DESTROY:
        WTSUnRegisterSessionNotification(w);
        UnregisterHotKey(w,Hotkey); Shell_NotifyIconW(NIM_DELETE,&tray); PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(w,msg,wp,lp);
}
template<WNDPROC Proc> LRESULT CALLBACK guardedProc(HWND w,UINT msg,WPARAM wp,LPARAM lp) noexcept {
    try { return Proc(w,msg,wp,lp); }
    catch(winrt::hresult_error const& e) { error(e.message().c_str(),w); }
    catch(std::exception const&) { error(L"This action could not be completed. Check folder access and available memory.",w); }
    catch(...) { error(L"This action could not be completed.",w); }
    return msg==WM_CREATE?-1:0;
}
}
int WINAPI wWinMain(HINSTANCE h,HINSTANCE,PWSTR,int) {
    instance=h; SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    HANDLE single=CreateMutexW(nullptr,FALSE,L"Local\\Glimpse.ScreenRecorder.v1");
    if(!single) return 1;
    if(GetLastError()==ERROR_ALREADY_EXISTS) { CloseHandle(single); return 0; }
    HRESULT ole=OleInitialize(nullptr); if(FAILED(ole)) { CloseHandle(single); return 1; }
    try {
        // VersionHelpers needs a manifest; verify the actual build before relying on capture exclusion.
        using VersionFn=LONG(WINAPI*)(OSVERSIONINFOW*);
        auto version=reinterpret_cast<VersionFn>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"),"RtlGetVersion"));
        OSVERSIONINFOW os{sizeof(os)};
        if(!version||version(&os)!=0||os.dwBuildNumber<19041) throw winrt::hresult_error(E_NOTIMPL,L"Glimpse requires Windows 10 version 2004 or newer, or Windows 11.");
        fontFace=os.dwBuildNumber>=22000?L"Segoe UI Variable Text":L"Segoe UI";
        INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_STANDARD_CLASSES|ICC_WIN95_CLASSES}; InitCommonControlsEx(&controls);
        DWORD light=1, bytes=sizeof(light);
        RegGetValueW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",L"AppsUseLightTheme",RRF_RT_REG_DWORD,nullptr,&light,&bytes);
        dark=light==0; bg=dark?RGB(28,28,32):RGB(249,249,250); fg=dark?RGB(245,245,247):RGB(27,27,31);
        background=CreateSolidBrush(bg);
        uiFont=CreateFontW(-16,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,fontFace);
        taskbarCreated=RegisterWindowMessageW(L"TaskbarCreated");
        WNDCLASSEXW cls{sizeof(cls)}; cls.hInstance=h; cls.hCursor=LoadCursorW(nullptr,IDC_ARROW); cls.hIcon=LoadIconW(h,MAKEINTRESOURCEW(1));
        cls.lpszClassName=L"GlimpseMain"; cls.lpfnWndProc=guardedProc<mainProc>; RegisterClassExW(&cls);
        cls.lpszClassName=L"GlimpseOverlay"; cls.lpfnWndProc=guardedProc<overlayProc>; RegisterClassExW(&cls);
        cls.lpszClassName=L"GlimpseFrame"; cls.lpfnWndProc=guardedProc<frameProc>; RegisterClassExW(&cls);
        cls.lpszClassName=L"GlimpsePanel"; cls.lpfnWndProc=guardedProc<panelProc>; cls.hbrBackground=background; RegisterClassExW(&cls);
        mainWindow=CreateWindowExW(WS_EX_TOOLWINDOW,L"GlimpseMain",L"Glimpse",WS_POPUP,0,0,0,0,nullptr,nullptr,h,nullptr);
        if(!mainWindow) winrt::throw_last_error();
        WTSRegisterSessionNotification(mainWindow,NOTIFY_FOR_THIS_SESSION);
        readConfig(); addTray();
        if(!registerKey(config.hotkey)) { settings(); error(L"The recording shortcut is already in use. Choose another in Settings.",settingsWindow); }
        MSG msg;
        while(GetMessageW(&msg,nullptr,0,0)>0) {
            HWND panel=settingsWindow?settingsWindow:toolbar?toolbar:shareWindow;
            if(msg.message==WM_KEYDOWN && panel && (msg.hwnd==panel||IsChild(panel,msg.hwnd))) {
                if(msg.wParam==VK_ESCAPE) { PostMessageW(panel,WM_CLOSE,0,0); continue; }
                if(msg.wParam==VK_RETURN && panel==toolbar) { if(state==State::Ready) startRecording(); continue; }
            }
            if(panel&&IsDialogMessageW(panel,&msg)) continue;
            TranslateMessage(&msg); DispatchMessageW(&msg);
        }
    } catch(winrt::hresult_error const& e) { error(e.message().c_str()); }
    catch(std::exception const& e) { std::string m=e.what(); error(std::wstring(m.begin(),m.end())); }
    recorder.stop(); recorder.join();
    if(uiFont) DeleteObject(uiFont); if(background) DeleteObject(background);
    OleUninitialize(); CloseHandle(single); return 0;
}
