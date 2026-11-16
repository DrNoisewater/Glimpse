#include "capture.hpp"
#include <windowsx.h>
#include <objidl.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <dwmapi.h>
#include <uxtheme.h>
#include <wtsapi32.h>
#include <winrt/base.h>
#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>
#include <sstream>
// GDI+ is used only for anti-aliased shapes; it expects min/max in its namespace.
namespace Gdiplus { using std::min; using std::max; }
#include <gdiplus.h>

using namespace glimpse;
namespace fs=std::filesystem;
namespace gp=Gdiplus;
namespace {
constexpr UINT TrayMessage=WM_APP+1, New=100, Recent=101, Folder=102, Settings=103, Exit=104;
constexpr UINT Record=200, Pause=201, Cancel=202, Copy=203, Reveal=204, Drag=205;
constexpr UINT Mic=206, SystemAudio=207, Dismiss=208, MicVolume=210, SystemVolume=211;
constexpr UINT AudioMenuBase=400, ToastTimer=4;
constexpr UINT Save=300, Browse=301, RecentBase=1000;
constexpr UINT Hotkey=1, RetryCopy=2, ClockTimer=3;
enum class State { Idle, Selecting, Ready, Starting, Recording, Paused, Saving };
HINSTANCE instance;
HWND mainWindow{}, overlay{}, toolbar{}, settingsWindow{}, shareWindow{};
HWND recordingFrame{}, recordingDot{}, micSlider{}, systemSlider{};
HWND actionButton{}, pauseButton{}, shareText{};
HFONT uiFont{};
HBRUSH background{}, fieldBrush{};
bool dark{}, win11{}, quitAfterSave{}, clipboardPending{}, clipboardCopied{};
COLORREF bg{}, fg{}, muted{}, surface{}, surfaceHot{}, surfacePressed{}, outline{};
UINT taskbarCreated{};
ULONG_PTR gdiplusToken{};
State state=State::Idle;
Rect area{}, desktop{};
POINT anchor{};
Recorder recorder;
NOTIFYICONDATAW tray{sizeof(tray)};
fs::path latestFile, configPath;
std::vector<fs::path> recentFiles;
std::wstring shareSummary, statusOverride;
int copyAttempts{};
unsigned liveAudio{};
bool audioArmed{}, sharingDrag{};
int liveMicVolume=100,liveSystemVolume=65;
ULONGLONG toastUntil{}, animationStart{};
const wchar_t* fontFace=L"Segoe UI";
const wchar_t* iconFace=L"Segoe MDL2 Assets";
// One accent family shared by the icon, selection overlay, panels and recording frame.
constexpr COLORREF Accent=RGB(124,92,255), AccentHot=RGB(143,116,255), AccentPressed=RGB(98,70,224);
constexpr COLORREF RecordRed=RGB(255,77,94), RecordHot=RGB(255,102,117), RecordPressed=RGB(219,52,70);
constexpr COLORREF Amber=RGB(245,176,65), Success=RGB(46,196,120), White=RGB(255,255,255);
struct Config {
    fs::path folder;
    Preset preset=Preset::Share;
    int fps=0;
    WORD hotkey=MAKEWORD('R',HOTKEYF_CONTROL|HOTKEYF_SHIFT);
    bool startup=false, software=false, diagnostics=false;
    unsigned audio=AudioSilent;
    int micVolume=100,systemVolume=65;
} config;
int scale(HWND w,int n) { return MulDiv(n,w?GetDpiForWindow(w):96,96); }
float scalef(HWND w,float n) { return n*float(w?GetDpiForWindow(w):96)/96.f; }
Rect rect(RECT r) { return {r.left,r.top,r.right,r.bottom}; }
RECT winRect(Rect r) { return {r.left,r.top,r.right,r.bottom}; }

// ---------- drawing helpers ----------
gp::Color color(COLORREF c,BYTE alpha=255) { return gp::Color(alpha,GetRValue(c),GetGValue(c),GetBValue(c)); }
void roundPath(gp::GraphicsPath& path,gp::RectF r,float radius) {
    radius=std::min({radius,r.Width/2,r.Height/2});
    if(radius<=.5f) { path.AddRectangle(r); return; }
    float d=radius*2;
    path.AddArc(r.X,r.Y,d,d,180,90); path.AddArc(r.X+r.Width-d,r.Y,d,d,270,90);
    path.AddArc(r.X+r.Width-d,r.Y+r.Height-d,d,d,0,90); path.AddArc(r.X,r.Y+r.Height-d,d,d,90,90);
    path.CloseFigure();
}
void fillRound(gp::Graphics& g,COLORREF c,BYTE alpha,gp::RectF r,float radius) {
    gp::GraphicsPath path; roundPath(path,r,radius); gp::SolidBrush brush(color(c,alpha)); g.FillPath(&brush,&path);
}
void strokeRound(gp::Graphics& g,COLORREF c,BYTE alpha,gp::RectF r,float radius,float width) {
    gp::GraphicsPath path; roundPath(path,r,radius); gp::Pen pen(color(c,alpha),width); g.DrawPath(&pen,&path);
}
void fillCircle(gp::Graphics& g,COLORREF c,BYTE alpha,float cx,float cy,float radius) {
    gp::SolidBrush brush(color(c,alpha)); g.FillEllipse(&brush,cx-radius,cy-radius,radius*2,radius*2);
}
void smooth(gp::Graphics& g) { g.SetSmoothingMode(gp::SmoothingModeAntiAlias); g.SetPixelOffsetMode(gp::PixelOffsetModeHalf); }
SIZE textSize(HDC dc,HFONT font,const std::wstring& text) {
    auto old=SelectObject(dc,font); SIZE s{}; GetTextExtentPoint32W(dc,text.c_str(),int(text.size()),&s); SelectObject(dc,old); return s;
}
void drawText(HDC dc,HFONT font,COLORREF c,const std::wstring& text,RECT r,UINT flags) {
    auto old=SelectObject(dc,font); SetBkMode(dc,TRANSPARENT); SetTextColor(dc,c);
    DrawTextW(dc,text.c_str(),int(text.size()),&r,flags|DT_NOPREFIX); SelectObject(dc,old);
}
float pulse() { // 0..1 breathing value for recording indicators
    double t=double((GetTickCount64()-animationStart)%1600)/1600.;
    return float(.5+.5*std::cos(t*6.283185307179586));
}

// ---------- fonts ----------
HFONT makeFont(int px,UINT dpi,int weight,const wchar_t* face) {
    return CreateFontW(-MulDiv(px,dpi,96),0,0,0,weight,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,face);
}
HFONT propFont(HWND w,const wchar_t* name) {
    auto f=reinterpret_cast<HFONT>(GetPropW(w,name)); return f?f:uiFont;
}
HFONT panelFont(HWND w) { return propFont(w,L"GlimpseFont"); }
void replaceFont(HWND w,const wchar_t* name,HFONT font) {
    auto old=reinterpret_cast<HFONT>(GetPropW(w,name)); SetPropW(w,name,font); if(old) DeleteObject(old);
}
void renewFont(HWND w,UINT dpi) {
    HFONT font=makeFont(14,dpi,FW_NORMAL,fontFace);
    for(HWND c=GetWindow(w,GW_CHILD);c;c=GetWindow(c,GW_HWNDNEXT)) SendMessageW(c,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);
    replaceFont(w,L"GlimpseFont",font);
    replaceFont(w,L"GlimpseHeading",makeFont(16,dpi,FW_SEMIBOLD,fontFace));
    replaceFont(w,L"GlimpseBold",makeFont(14,dpi,FW_SEMIBOLD,fontFace));
    replaceFont(w,L"GlimpseSmall",makeFont(12,dpi,FW_NORMAL,fontFace));
    replaceFont(w,L"GlimpseIcons",makeFont(16,dpi,FW_NORMAL,iconFace));
}
void dropFonts(HWND w) {
    for(auto name:{L"GlimpseFont",L"GlimpseHeading",L"GlimpseBold",L"GlimpseSmall",L"GlimpseIcons"})
        if(auto f=RemovePropW(w,name)) DeleteObject(f);
}

std::wstring windowText(HWND w) {
    int n=GetWindowTextLengthW(w); std::wstring s(size_t(n)+1,L'\0');
    GetWindowTextW(w,s.data(),n+1); s.resize(n); return s;
}
void error(const std::wstring& text, HWND parent=nullptr) {
    MessageBoxW(parent?parent:mainWindow,text.c_str(),L"Glimpse",MB_OK|MB_ICONERROR);
}
void themeWindow(HWND w) {
    BOOL value=dark; DwmSetWindowAttribute(w,20,&value,sizeof(value)); // immersive dark title bar
    DWORD corners=2; DwmSetWindowAttribute(w,33,&corners,sizeof(corners)); // Windows 11 rounded corners
    if(win11) { COLORREF border=outline; DwmSetWindowAttribute(w,34,&border,sizeof(border)); }
    MARGINS shadow{1,1,1,1}; DwmExtendFrameIntoClientArea(w,&shadow);
}
void darkControl(HWND c) { if(dark) SetWindowTheme(c,L"DarkMode_CFD",nullptr); }
void exclude(HWND w) {
    if(!SetWindowDisplayAffinity(w,0x00000011))
        throw winrt::hresult_error(HRESULT_FROM_WIN32(GetLastError()),L"Windows could not exclude the recorder controls from capture.");
}
std::wstring hotkeyText(WORD key) {
    std::wstring s; BYTE flags=HIBYTE(key);
    if(flags&HOTKEYF_CONTROL) s+=L"Ctrl+";
    if(flags&HOTKEYF_ALT) s+=L"Alt+";
    if(flags&HOTKEYF_SHIFT) s+=L"Shift+";
    UINT vk=LOBYTE(key); LONG code=LONG(MapVirtualKeyW(vk,MAPVK_VK_TO_VSC))<<16;
    if(flags&HOTKEYF_EXT) code|=1<<24;
    wchar_t name[64]{};
    if(GetKeyNameTextW(code,name,int(std::size(name)))>0) s+=name; else s+=wchar_t(vk);
    return s;
}
void balloon(const std::wstring& title,const std::wstring& body) {
    tray.uFlags=NIF_INFO; wcsncpy_s(tray.szInfoTitle,title.c_str(),_TRUNCATE);
    wcsncpy_s(tray.szInfo,body.c_str(),_TRUNCATE); tray.dwInfoFlags=NIIF_INFO;
    Shell_NotifyIconW(NIM_MODIFY,&tray);
}
void updateTrayTip() {
    tray.uFlags=NIF_TIP;
    wcsncpy_s(tray.szTip,(L"Glimpse — press "+hotkeyText(config.hotkey)+L" to record").c_str(),_TRUNCATE);
    Shell_NotifyIconW(NIM_MODIFY,&tray);
}
void addTray() {
    tray.hWnd=mainWindow; tray.uID=1; tray.uFlags=NIF_MESSAGE|NIF_ICON|NIF_TIP;
    tray.uCallbackMessage=TrayMessage;
    tray.hIcon=static_cast<HICON>(LoadImageW(instance,MAKEINTRESOURCEW(1),IMAGE_ICON,
        GetSystemMetrics(SM_CXSMICON),GetSystemMetrics(SM_CYSMICON),LR_DEFAULTCOLOR));
    wcsncpy_s(tray.szTip,(L"Glimpse — press "+hotkeyText(config.hotkey)+L" to record").c_str(),_TRUNCATE);
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
    config.diagnostics=GetPrivateProfileIntW(L"Settings",L"Diagnostics",0,configPath.c_str())!=0;
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
    put(L"Diagnostics",c.diagnostics?L"1":L"0");
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

// ---------- controls ----------
LRESULT CALLBACK hoverProc(HWND w,UINT msg,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR) {
    switch(msg) {
    case WM_MOUSEMOVE:
        if(!GetPropW(w,L"GlimpseHot")) {
            SetPropW(w,L"GlimpseHot",reinterpret_cast<HANDLE>(1));
            TRACKMOUSEEVENT track{sizeof(track),TME_LEAVE,w,0}; TrackMouseEvent(&track);
            InvalidateRect(w,nullptr,FALSE);
        } break;
    case WM_MOUSELEAVE: RemovePropW(w,L"GlimpseHot"); InvalidateRect(w,nullptr,FALSE); break;
    // Owner-drawn buttons turn fast second clicks into BN_DOUBLECLICKED; treat them as clicks.
    case WM_LBUTTONDBLCLK: return DefSubclassProc(w,WM_LBUTTONDOWN,wp,lp);
    case WM_NCDESTROY: RemovePropW(w,L"GlimpseHot"); RemoveWindowSubclass(w,hoverProc,2); break;
    }
    return DefSubclassProc(w,msg,wp,lp);
}
HWND child(HWND parent,const wchar_t* cls,const wchar_t* text,int id,int x,int y,int w,int h,DWORD extra=0) {
    HWND c=CreateWindowExW(0,cls,text,WS_CHILD|WS_VISIBLE|extra,scale(parent,x),scale(parent,y),scale(parent,w),scale(parent,h),parent,
        reinterpret_cast<HMENU>(INT_PTR(id)),instance,nullptr);
    SendMessageW(c,WM_SETFONT,reinterpret_cast<WPARAM>(panelFont(parent)),TRUE); return c;
}
HWND button(HWND p,const wchar_t* text,int id,int x,int y,int w,int h=34) {
    HWND b=child(p,L"BUTTON",text,id,x,y,w,h,WS_TABSTOP|BS_OWNERDRAW);
    SetWindowSubclass(b,hoverProc,2,0); return b;
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
    HWND s=child(parent,TRACKBAR_CLASSW,name,id,x,y,width,26,WS_TABSTOP|TBS_HORZ|TBS_NOTICKS|TBS_BOTH);
    SendMessageW(s,TBM_SETRANGE,TRUE,MAKELPARAM(0,100)); SendMessageW(s,TBM_SETPOS,TRUE,volume);
    SendMessageW(s,TBM_SETPAGESIZE,0,10); tip(s,name); return s;
}

// ---------- recording toolbar layout (DIPs) ----------
struct ToolbarLayout {
    int width, height, buttons; // buttons: x of the first round button
    static constexpr int Row=56, AudioRow=50, Button=36, Gap=6, Pad=12;
};
ToolbarLayout toolbarLayout() {
    ToolbarLayout l{};
    l.width=audioArmed?424:312; l.height=audioArmed?ToolbarLayout::Row+ToolbarLayout::AudioRow:ToolbarLayout::Row;
    l.buttons=l.width-ToolbarLayout::Pad-3*ToolbarLayout::Button-2*ToolbarLayout::Gap;
    return l;
}
// Per-source positions on the audio row.
struct AudioSlot { int toggle, slider, meter, percent; };
constexpr AudioSlot audioSlots[2]{{12,50,56,170},{214,252,258,372}};
constexpr int SliderWidth=118, MeterWidth=106;
RECT statusRect(HWND w) { return {scale(w,12),scale(w,10),scale(w,toolbarLayout().buttons-6),scale(w,46)}; }

void invalidateToolbar(bool status=true,bool audio=true) {
    if(!toolbar) return;
    if(status) { RECT r=statusRect(toolbar); InvalidateRect(toolbar,&r,FALSE); }
    if(audio&&audioArmed) {
        for(auto& slot:audioSlots) {
            RECT meter{scale(toolbar,slot.meter-2),scale(toolbar,ToolbarLayout::Row+34),scale(toolbar,slot.meter+MeterWidth+2),scale(toolbar,ToolbarLayout::Row+42)};
            RECT pct{scale(toolbar,slot.percent),scale(toolbar,ToolbarLayout::Row+6),scale(toolbar,slot.percent+40),scale(toolbar,ToolbarLayout::Row+40)};
            InvalidateRect(toolbar,&meter,FALSE); InvalidateRect(toolbar,&pct,FALSE);
        }
    }
}
void audioControls() {
    if(!toolbar) return;
    SetDlgItemTextW(toolbar,Mic,(liveAudio&AudioMic)?L"Microphone on":L"Microphone muted");
    SetDlgItemTextW(toolbar,SystemAudio,(liveAudio&AudioSystem)?L"System sound on":L"System sound muted");
    bool enable=audioArmed&&(state==State::Recording||state==State::Paused);
    for(UINT id:{Mic,SystemAudio,MicVolume,SystemVolume}) if(HWND c=GetDlgItem(toolbar,id)) { EnableWindow(c,enable); InvalidateRect(c,nullptr,FALSE); }
    invalidateToolbar(false,true);
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
        if(shareWindow) SetWindowTextW(shareText,(shareSummary+L"\nCopied — paste it anywhere that accepts files.").c_str());
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

// ---------- recording frame and status dot (both excluded from capture) ----------
COLORREF stateColor() {
    return state==State::Paused?Amber:state==State::Recording?RecordRed:Accent;
}
void updateDot() {
    if(!recordingDot) return;
    const int size=scale(recordingDot,30);
    BITMAPINFO info{}; info.bmiHeader={sizeof(BITMAPINFOHEADER),size,-size,1,32,BI_RGB};
    void* bits=nullptr; HDC screen=GetDC(nullptr); HDC mem=CreateCompatibleDC(screen);
    HBITMAP dib=CreateDIBSection(screen,&info,DIB_RGB_COLORS,&bits,nullptr,0);
    if(dib&&bits) {
        auto old=SelectObject(mem,dib);
        {
            gp::Bitmap canvas(size,size,size*4,PixelFormat32bppPARGB,static_cast<BYTE*>(bits));
            gp::Graphics g(&canvas); smooth(g); g.Clear(gp::Color(0,0,0,0));
            float c=size/2.f, core=scalef(recordingDot,6.f);
            COLORREF col=stateColor();
            if(state==State::Recording) {
                float p=1.f-pulse(); // expanding, fading halo
                fillCircle(g,col,BYTE(110*(1.f-p)),c,c,core+(c-core-1)*p);
            } else fillCircle(g,col,70,c,c,core+scalef(recordingDot,4.f));
            fillCircle(g,RGB(20,20,24),120,c,c,core+scalef(recordingDot,1.5f));
            fillCircle(g,col,255,c,c,core);
        }
        POINT origin{0,0}; SIZE extent{size,size}; BLENDFUNCTION blend{AC_SRC_OVER,0,255,AC_SRC_ALPHA};
        RECT r; GetWindowRect(recordingDot,&r); POINT at{r.left,r.top};
        UpdateLayeredWindow(recordingDot,screen,&at,&extent,mem,&origin,0,&blend,ULW_ALPHA);
        SelectObject(mem,old);
    }
    if(dib) DeleteObject(dib);
    DeleteDC(mem); ReleaseDC(nullptr,screen);
}
void refreshIndicators() {
    if(recordingFrame) InvalidateRect(recordingFrame,nullptr,FALSE);
    updateDot(); invalidateToolbar(true,false);
    if(pauseButton) InvalidateRect(pauseButton,nullptr,FALSE);
}
void closeSelection() {
    if(overlay) { DestroyWindow(overlay); overlay=nullptr; }
    if(toolbar) { DestroyWindow(toolbar); toolbar=nullptr; }
    if(recordingFrame) { DestroyWindow(recordingFrame); recordingFrame=nullptr; }
    if(recordingDot) { DestroyWindow(recordingDot); recordingDot=nullptr; }
    pauseButton=actionButton=micSlider=systemSlider=nullptr;
    KillTimer(mainWindow,ClockTimer); statusOverride.clear(); state=State::Idle;
}
void stopRecording(bool cancel=false) {
    if(state==State::Ready||state==State::Selecting) { closeSelection(); return; }
    if(state!=State::Recording&&state!=State::Paused&&state!=State::Starting) return;
    state=State::Saving; recorder.stop(cancel);
    statusOverride=cancel?L"Discarding…":L"Saving…";
    audioControls(); refreshIndicators();
    EnableWindow(actionButton,FALSE); EnableWindow(pauseButton,FALSE);
    EnableWindow(GetDlgItem(toolbar,Cancel),FALSE);
}
void makeFrame() {
    const int gap=2, thick=2, pad=gap+thick;
    int width=area.width()+pad*2,height=area.height()+pad*2;
    recordingFrame=CreateWindowExW(WS_EX_LAYERED|WS_EX_TRANSPARENT|WS_EX_NOACTIVATE|WS_EX_TOPMOST|WS_EX_TOOLWINDOW,
        L"GlimpseFrame",L"Recording boundary",WS_POPUP,area.left-pad,area.top-pad,width,height,
        mainWindow,nullptr,instance,nullptr);
    if(!recordingFrame) winrt::throw_last_error();
    // A thin ring just outside the captured pixels; repainted only when the state changes.
    HRGN ring=CreateRectRgn(0,0,width,height),hole=CreateRectRgn(thick,thick,width-thick,height-thick);
    CombineRgn(ring,ring,hole,RGN_DIFF); DeleteObject(hole);
    if(!SetWindowRgn(recordingFrame,ring,FALSE)) DeleteObject(ring);
    SetLayeredWindowAttributes(recordingFrame,0,235,LWA_ALPHA); exclude(recordingFrame);
    ShowWindow(recordingFrame,SW_SHOWNOACTIVATE);
    if(area.width()>=96&&area.height()>=96) {
        recordingDot=CreateWindowExW(WS_EX_LAYERED|WS_EX_TRANSPARENT|WS_EX_NOACTIVATE|WS_EX_TOPMOST|WS_EX_TOOLWINDOW,
            L"GlimpseFrame",L"Recording indicator",WS_POPUP,area.right-40,area.top+10,30,30,mainWindow,nullptr,instance,nullptr);
        if(!recordingDot) winrt::throw_last_error();
        int size=scale(recordingDot,30),inset=scale(recordingDot,10);
        SetWindowPos(recordingDot,HWND_TOPMOST,area.right-inset-size,area.top+inset,size,size,SWP_NOACTIVATE);
        exclude(recordingDot); updateDot(); ShowWindow(recordingDot,SW_SHOWNOACTIVATE);
    }
}
void positionPopup(HWND w,HMONITOR monitor,int preferredX,int preferredY,int width,int height) {
    MONITORINFO info{sizeof(info)}; GetMonitorInfoW(monitor,&info);
    int sw=scale(w,width), sh=scale(w,height);
    int x=std::clamp(preferredX,int(info.rcWork.left),std::max(int(info.rcWork.left),int(info.rcWork.right)-sw));
    int y=std::clamp(preferredY,int(info.rcWork.top),std::max(int(info.rcWork.top),int(info.rcWork.bottom)-sh));
    SetWindowPos(w,HWND_TOPMOST,x,y,sw,sh,SWP_NOACTIVATE);
}
void makeToolbar() {
    RECT selected=winRect(area);
    auto monitor=MonitorFromRect(&selected,MONITOR_DEFAULTTONEAREST);
    auto layout=toolbarLayout();
    toolbar=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_TOPMOST,L"GlimpsePanel",L"Glimpse — recording controls",
        WS_POPUP|WS_CLIPCHILDREN,area.right-layout.width,area.top,layout.width,layout.height,mainWindow,nullptr,instance,nullptr);
    if(!toolbar) winrt::throw_last_error();
    themeWindow(toolbar);
    // Prefer just above the region, then just below it, then inside its bottom-right corner.
    MONITORINFO mi{sizeof(mi)}; GetMonitorInfoW(monitor,&mi);
    int w=scale(toolbar,layout.width), h=scale(toolbar,layout.height), gap=scale(toolbar,10);
    int y=area.top-h-gap;
    if(y<mi.rcWork.top) y=area.bottom+gap;
    if(y+h>mi.rcWork.bottom) y=area.bottom-h-scale(toolbar,16);
    positionPopup(toolbar,monitor,area.right-w,y,layout.width,layout.height);
    const int b=ToolbarLayout::Button, top=(ToolbarLayout::Row-b)/2;
    pauseButton=button(toolbar,L"Pause",Pause,layout.buttons,top,b,b); EnableWindow(pauseButton,FALSE);
    actionButton=button(toolbar,L"Stop and save",Record,layout.buttons+b+ToolbarLayout::Gap,top,b,b);
    auto discard=button(toolbar,L"Discard",Cancel,layout.buttons+2*(b+ToolbarLayout::Gap),top,b,b);
    tip(pauseButton,L"Pause or resume. Paused time is left out of the video.");
    tip(actionButton,L"Stop and save the recording.");
    tip(discard,L"Discard this recording without saving it.");
    if(audioArmed) {
        const int y2=ToolbarLayout::Row+6;
        auto mic=button(toolbar,L"Microphone",Mic,audioSlots[0].toggle,y2,34,34);
        micSlider=slider(toolbar,MicVolume,audioSlots[0].slider,y2+4,SliderWidth,liveMicVolume,L"Microphone volume. Use arrow keys to adjust.");
        auto system=button(toolbar,L"System sound",SystemAudio,audioSlots[1].toggle,y2,34,34);
        systemSlider=slider(toolbar,SystemVolume,audioSlots[1].slider,y2+4,SliderWidth,liveSystemVolume,L"System sound volume. Lower it to keep narration clear.");
        tip(mic,L"Mute or unmute your microphone. The bar shows the live input level.");
        tip(system,L"Mute or unmute sound from your speakers or headphones.");
    }
    ShowWindow(toolbar,SW_SHOWNOACTIVATE);
}
void startRecording() {
    if(state!=State::Ready) return;
    try {
        auto spec=videoSpec(area.width(),area.height(),config.preset,config.fps);
        liveAudio=config.audio; audioArmed=liveAudio!=AudioSilent;
        liveMicVolume=config.micVolume; liveSystemVolume=config.systemVolume;
        statusOverride.clear(); animationStart=GetTickCount64();
        makeToolbar(); exclude(toolbar);
        if(overlay) { DestroyWindow(overlay); overlay=nullptr; }
        state=State::Starting; makeFrame(); audioControls();
        recorder.start(mainWindow,area,spec,config.folder,config.software,liveAudio,config.diagnostics);
        SetTimer(mainWindow,ClockTimer,50,nullptr);
    } catch(winrt::hresult_error const& e) { closeSelection(); error(e.message().c_str()); }
    catch(std::exception const& e) { closeSelection(); std::string m=e.what(); error(std::wstring(m.begin(),m.end())); }
}

// ---------- region selection overlay ----------
// The overlay shows a frozen copy of the desktop (like Snipping Tool): a dimmed
// copy everywhere and the original pixels inside the selection. Painting is
// double-buffered and only the changed bands are invalidated, so dragging does
// not flicker and the selection edge is always clearly visible.
constexpr const wchar_t* HintText=L"Drag to select an area   ·   Click a window or screen   ·   Esc to cancel";
struct Decor {
    bool has{}; int kind{}; // 0 = dragged region, 1 = window, 2 = whole display
    RECT focus{}, pill{}, hint{};
    std::wstring pillText;
};
struct Selector {
    HDC screen{}, dimmed{};
    HBITMAP screenBitmap{}, dimBitmap{};
    HGDIOBJ oldScreen{}, oldDim{};
    std::vector<std::pair<Rect,int>> targets; // windows in z-order, then monitors
    Rect hover{}; int hoverKind{};
    bool pressed{}, moved{};
    Decor shown;
} sel;
void releaseSelector() {
    if(sel.screen) { SelectObject(sel.screen,sel.oldScreen); DeleteDC(sel.screen); }
    if(sel.dimmed) { SelectObject(sel.dimmed,sel.oldDim); DeleteDC(sel.dimmed); }
    if(sel.screenBitmap) DeleteObject(sel.screenBitmap);
    if(sel.dimBitmap) DeleteObject(sel.dimBitmap);
    sel=Selector{};
}
BOOL CALLBACK collectWindow(HWND w,LPARAM) {
    if(!IsWindowVisible(w)||IsIconic(w)) return TRUE;
    DWORD pid=0; GetWindowThreadProcessId(w,&pid); if(pid==GetCurrentProcessId()) return TRUE;
    BOOL cloaked=FALSE; DwmGetWindowAttribute(w,DWMWA_CLOAKED,&cloaked,sizeof(cloaked)); if(cloaked) return TRUE;
    if(GetWindowLongW(w,GWL_EXSTYLE)&WS_EX_TRANSPARENT) return TRUE;
    wchar_t cls[64]{}; GetClassNameW(w,cls,int(std::size(cls)));
    if(!wcscmp(cls,L"Progman")||!wcscmp(cls,L"WorkerW")) return TRUE; // desktop: use the monitor instead
    RECT r{};
    if(FAILED(DwmGetWindowAttribute(w,DWMWA_EXTENDED_FRAME_BOUNDS,&r,sizeof(r)))) GetWindowRect(w,&r);
    Rect bounds=intersect(rect(r),desktop);
    if(bounds.width()>=48&&bounds.height()>=48) sel.targets.push_back({bounds,1});
    return TRUE;
}
BOOL CALLBACK collectMonitor(HMONITOR,HDC,LPRECT r,LPARAM) { sel.targets.push_back({rect(*r),2}); return TRUE; }
void prepareSelector() {
    releaseSelector();
    const int w=desktop.width(), h=desktop.height();
    HDC screenDc=GetDC(nullptr);
    sel.screen=CreateCompatibleDC(screenDc); sel.dimmed=CreateCompatibleDC(screenDc);
    sel.screenBitmap=CreateCompatibleBitmap(screenDc,w,h); sel.dimBitmap=CreateCompatibleBitmap(screenDc,w,h);
    if(!sel.screen||!sel.dimmed||!sel.screenBitmap||!sel.dimBitmap) {
        ReleaseDC(nullptr,screenDc); releaseSelector();
        throw winrt::hresult_error(E_OUTOFMEMORY,L"Not enough graphics memory to show the selection overlay.");
    }
    sel.oldScreen=SelectObject(sel.screen,sel.screenBitmap); sel.oldDim=SelectObject(sel.dimmed,sel.dimBitmap);
    BitBlt(sel.screen,0,0,w,h,screenDc,desktop.left,desktop.top,SRCCOPY);
    ReleaseDC(nullptr,screenDc);
    BitBlt(sel.dimmed,0,0,w,h,sel.screen,0,0,SRCCOPY);
    { gp::Graphics g(sel.dimmed); gp::SolidBrush shade(gp::Color(120,6,6,12)); g.FillRectangle(&shade,0,0,w,h); }
    EnumWindows(collectWindow,0);
    EnumDisplayMonitors(nullptr,nullptr,collectMonitor,0);
}
std::pair<Rect,int> targetAt(POINT p) {
    for(auto& t:sel.targets)
        if(p.x>=t.first.left&&p.x<t.first.right&&p.y>=t.first.top&&p.y<t.first.bottom) return t;
    return {{},0};
}
RECT toClient(Rect r) { return {r.left-desktop.left,r.top-desktop.top,r.right-desktop.left,r.bottom-desktop.top}; }
Decor layoutDecor(HWND w) {
    Decor d;
    POINT cursor; GetCursorPos(&cursor);
    HDC dc=GetDC(w); HFONT bold=propFont(w,L"GlimpseBold"), font=panelFont(w);
    const int padX=scale(w,12), pillH=scale(w,30), gap=scale(w,10);
    // Hint pill: top centre of the display under the cursor, hidden while dragging.
    if(!(sel.pressed&&sel.moved)) {
        MONITORINFO mi{sizeof(mi)}; GetMonitorInfoW(MonitorFromPoint(cursor,MONITOR_DEFAULTTONEAREST),&mi);
        SIZE s=textSize(dc,font,HintText); int width=s.cx+padX*2+scale(w,22), cx=(mi.rcMonitor.left+mi.rcMonitor.right)/2-desktop.left;
        int top=mi.rcMonitor.top-desktop.top+scale(w,28);
        d.hint={cx-width/2,top,cx-width/2+width,top+scale(w,36)};
    }
    Rect focus{}; int kind=0;
    if(sel.pressed&&sel.moved) focus=area;
    else if(sel.hover.width()>0) { focus=sel.hover; kind=sel.hoverKind; }
    if(focus.width()>0&&focus.height()>0) {
        d.has=true; d.kind=kind; d.focus=toClient(focus);
        d.pillText=(kind==1?L"Window   ":kind==2?L"Screen   ":L"")+std::to_wstring(focus.width())+L" × "+std::to_wstring(focus.height());
        SIZE s=textSize(dc,bold,d.pillText); int width=s.cx+padX*2;
        RECT f=winRect(focus);
        MONITORINFO mi{sizeof(mi)}; GetMonitorInfoW(MonitorFromRect(&f,MONITOR_DEFAULTTONEAREST),&mi);
        int x=focus.left, y=focus.bottom+gap;
        if(y+pillH>mi.rcMonitor.bottom) { x=focus.left+gap; y=focus.bottom-gap-pillH; }
        x=std::clamp(x,int(mi.rcMonitor.left)+gap,std::max(int(mi.rcMonitor.left)+gap,int(mi.rcMonitor.right)-width-gap));
        d.pill={x-desktop.left,y-desktop.top,x-desktop.left+width,y-desktop.top+pillH};
    }
    ReleaseDC(w,dc);
    return d;
}
void invalidateDecor(HWND w,const Decor& d) {
    HRGN acc=CreateRectRgn(0,0,0,0);
    auto add=[&](RECT r) { HRGN x=CreateRectRgnIndirect(&r); CombineRgn(acc,acc,x,RGN_OR); DeleteObject(x); };
    if(d.has) {
        // Only the outline band changes appearance; the interior is handled by the XOR below.
        int m=scale(w,10); RECT out=d.focus,in=d.focus; InflateRect(&out,m,m); InflateRect(&in,-m,-m);
        HRGN band=CreateRectRgnIndirect(&out);
        if(in.right>in.left&&in.bottom>in.top) { HRGN hole=CreateRectRgnIndirect(&in); CombineRgn(band,band,hole,RGN_DIFF); DeleteObject(hole); }
        CombineRgn(acc,acc,band,RGN_OR); DeleteObject(band);
        RECT pill=d.pill; InflateRect(&pill,2,2); add(pill);
    }
    if(d.hint.right>d.hint.left) { RECT hint=d.hint; InflateRect(&hint,2,2); add(hint); }
    InvalidateRgn(w,acc,FALSE); DeleteObject(acc);
}
bool sameDecor(const Decor& a,const Decor& b) {
    return a.has==b.has&&a.kind==b.kind&&EqualRect(&a.focus,&b.focus)&&EqualRect(&a.pill,&b.pill)&&
        EqualRect(&a.hint,&b.hint)&&a.pillText==b.pillText;
}
void refreshOverlay(HWND w) {
    Decor next=layoutDecor(w);
    if(sameDecor(next,sel.shown)) return; // most mouse moves change nothing on screen
    invalidateDecor(w,sel.shown); invalidateDecor(w,next);
    // Pixels that switch between dimmed and clear.
    RECT empty{};
    HRGN before=CreateRectRgnIndirect(sel.shown.has?&sel.shown.focus:&empty), after=CreateRectRgnIndirect(next.has?&next.focus:&empty);
    HRGN changed=CreateRectRgn(0,0,0,0); CombineRgn(changed,before,after,RGN_XOR);
    InvalidateRgn(w,changed,FALSE); DeleteObject(before); DeleteObject(after); DeleteObject(changed);
    sel.shown=std::move(next);
}
void paintOverlay(HWND w,HDC target,RECT paint) {
    const int pw=paint.right-paint.left, ph=paint.bottom-paint.top;
    if(pw<=0||ph<=0) return;
    HDC mem=CreateCompatibleDC(target); HBITMAP bitmap=CreateCompatibleBitmap(target,pw,ph); auto old=SelectObject(mem,bitmap);
    const int ox=paint.left, oy=paint.top;
    BitBlt(mem,0,0,pw,ph,sel.dimmed,ox,oy,SRCCOPY);
    const Decor& d=sel.shown;
    RECT clear{};
    if(d.has&&IntersectRect(&clear,&d.focus,&paint))
        BitBlt(mem,clear.left-ox,clear.top-oy,clear.right-clear.left,clear.bottom-clear.top,sel.screen,clear.left,clear.top,SRCCOPY);
    auto local=[&](RECT r) { return gp::RectF(float(r.left-ox),float(r.top-oy),float(r.right-r.left),float(r.bottom-r.top)); };
    {
        gp::Graphics g(mem); smooth(g);
        if(d.has) {
            float stroke=scalef(w,2.f); gp::RectF f=local(d.focus);
            f.Inflate(stroke/2,stroke/2);
            gp::Pen pen(color(Accent),stroke); g.DrawRectangle(&pen,f);
            if(d.kind==0) { // corner handles on a dragged region
                float r=scalef(w,4.5f);
                for(float x:{f.X,f.X+f.Width}) for(float y:{f.Y,f.Y+f.Height}) {
                    fillCircle(g,White,255,x,y,r); gp::Pen ring(color(Accent),scalef(w,2.f)); g.DrawEllipse(&ring,x-r,y-r,r*2,r*2);
                }
            }
            fillRound(g,RGB(18,18,24),225,local(d.pill),scalef(w,8.f));
        }
        if(d.hint.right>d.hint.left) {
            gp::RectF h=local(d.hint);
            fillRound(g,RGB(18,18,24),232,h,h.Height/2);
            strokeRound(g,White,40,h,h.Height/2,1.f);
            fillCircle(g,RecordRed,255,h.X+scalef(w,18.f),h.Y+h.Height/2,scalef(w,4.5f));
        }
    }
    auto shift=[&](RECT r) { OffsetRect(&r,-ox,-oy); return r; };
    if(d.has) drawText(mem,propFont(w,L"GlimpseBold"),White,d.pillText,shift(d.pill),DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    if(d.hint.right>d.hint.left) {
        RECT h=shift(d.hint); h.left+=scale(w,22);
        drawText(mem,panelFont(w),RGB(236,236,242),HintText,h,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    }
    BitBlt(target,paint.left,paint.top,pw,ph,mem,0,0,SRCCOPY);
    SelectObject(mem,old); DeleteObject(bitmap); DeleteDC(mem);
}
void newRecording() {
    if(state!=State::Idle) {
        if(state==State::Selecting) closeSelection();
        else if(state==State::Ready) startRecording();
        else if(state==State::Recording||state==State::Paused||state==State::Starting) stopRecording();
        return;
    }
    if(settingsWindow) { SetForegroundWindow(settingsWindow); return; }
    if(shareWindow) { DestroyWindow(shareWindow); shareWindow=nullptr; }
    desktop={GetSystemMetrics(SM_XVIRTUALSCREEN),GetSystemMetrics(SM_YVIRTUALSCREEN),0,0};
    desktop.right=desktop.left+GetSystemMetrics(SM_CXVIRTUALSCREEN); desktop.bottom=desktop.top+GetSystemMetrics(SM_CYVIRTUALSCREEN);
    area={}; prepareSelector(); state=State::Selecting;
    overlay=CreateWindowExW(WS_EX_TOPMOST|WS_EX_TOOLWINDOW,L"GlimpseOverlay",L"Glimpse — select a region to record",
        WS_POPUP,desktop.left,desktop.top,desktop.width(),desktop.height(),nullptr,nullptr,instance,nullptr);
    if(!overlay) { state=State::Idle; releaseSelector(); winrt::throw_last_error(); }
    // Keep the frozen overlay out of the first captured frames while it is being torn down.
    SetWindowDisplayAffinity(overlay,0x00000011);
    POINT p; GetCursorPos(&p); auto t=targetAt(p); sel.hover=t.first; sel.hoverKind=t.second;
    sel.shown=layoutDecor(overlay);
    ShowWindow(overlay,SW_SHOW); SetForegroundWindow(overlay); SetFocus(overlay);
}
void beginFromSelection(Rect chosen) {
    area=intersect(chosen,desktop);
    if(area.width()<8||area.height()<8) { area={}; return; }
    state=State::Ready; startRecording();
}
LRESULT CALLBACK overlayProc(HWND w,UINT msg,WPARAM wp,LPARAM lp) {
    switch(msg) {
    case WM_CREATE:
        renewFont(w,GetDpiForWindow(w)); return 0;
    case WM_SETCURSOR: SetCursor(LoadCursorW(nullptr,IDC_CROSS)); return TRUE;
    case WM_ERASEBKGND: return 1;
    case WM_LBUTTONDOWN:
        if(state!=State::Selecting) return 0;
        GetCursorPos(&anchor); sel.pressed=true; sel.moved=false; SetCapture(w); return 0;
    case WM_MOUSEMOVE: {
        if(state!=State::Selecting) return 0;
        POINT p; GetCursorPos(&p);
        if(sel.pressed) {
            int dx=p.x-anchor.x, dy=p.y-anchor.y;
            if(!sel.moved && (std::abs(dx)>=scale(w,4)||std::abs(dy)>=scale(w,4))) sel.moved=true;
            if(sel.moved) {
                if(GetKeyState(VK_SHIFT)<0) { // Shift keeps the region square
                    int side=std::max(std::abs(dx),std::abs(dy));
                    p.x=anchor.x+(dx<0?-side:side); p.y=anchor.y+(dy<0?-side:side);
                }
                area=intersect(normalize(anchor.x,anchor.y,p.x,p.y),desktop);
            }
        } else {
            auto t=targetAt(p); sel.hover=t.first; sel.hoverKind=t.second;
        }
        refreshOverlay(w); return 0;
    }
    case WM_LBUTTONUP:
        if(sel.pressed) {
            sel.pressed=false; ReleaseCapture();
            Rect chosen=sel.moved?area:sel.hover;
            sel.moved=false; beginFromSelection(chosen);
            if(state==State::Selecting) refreshOverlay(w);
        } return 0;
    case WM_RBUTTONDOWN:
        if(sel.pressed) { sel.pressed=false; sel.moved=false; ReleaseCapture(); area={}; refreshOverlay(w); }
        else closeSelection();
        return 0;
    case WM_CAPTURECHANGED:
        if(sel.pressed&&reinterpret_cast<HWND>(lp)!=w) { sel.pressed=false; sel.moved=false; area={}; refreshOverlay(w); }
        return 0;
    case WM_KEYDOWN: if(wp==VK_ESCAPE) closeSelection(); return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC dc=BeginPaint(w,&ps);
        if(sel.screen) paintOverlay(w,dc,ps.rcPaint);
        EndPaint(w,&ps); return 0;
    }
    case WM_DISPLAYCHANGE: closeSelection(); return 0;
    case WM_NCDESTROY: dropFonts(w); releaseSelector(); break;
    }
    return DefWindowProcW(w,msg,wp,lp);
}

// ---------- share toast ----------
constexpr int ShareWidth=404, ShareHeight=176;
void showShare() {
    if(latestFile.empty()) return;
    if(shareWindow) { SetForegroundWindow(shareWindow); return; }
    POINT point{}; GetCursorPos(&point); auto m=MonitorFromPoint(point,MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{sizeof(mi)}; GetMonitorInfoW(m,&mi);
    shareWindow=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_TOPMOST|WS_EX_NOACTIVATE,L"GlimpsePanel",L"Glimpse — capture saved",
        WS_POPUP|WS_CLIPCHILDREN,point.x,point.y,ShareWidth,ShareHeight,mainWindow,nullptr,instance,nullptr);
    themeWindow(shareWindow);
    positionPopup(shareWindow,m,mi.rcWork.right-scale(shareWindow,ShareWidth+16),mi.rcWork.bottom-scale(shareWindow,ShareHeight+16),ShareWidth,ShareHeight);
    auto close=button(shareWindow,L"Dismiss",Dismiss,ShareWidth-44,12,30,30);
    tip(close,L"Dismiss. Your file stays in Recent Captures.");
    auto message=shareSummary+(clipboardPending?L"\nCopying file…":clipboardCopied?L"\nCopied — paste it anywhere that accepts files.":L"\nCopy or drag the file to share it.");
    shareText=child(shareWindow,L"STATIC",message.c_str(),215,52,44,ShareWidth-70,62,SS_NOPREFIX|SS_ENDELLIPSIS);
    button(shareWindow,L"Copy file",Copy,18,ShareHeight-52,112);
    button(shareWindow,L"Show in folder",Reveal,138,ShareHeight-52,132);
    auto drag=button(shareWindow,L"Drag",Drag,278,ShareHeight-52,ShareWidth-296); SetWindowSubclass(drag,dragProc,1,0);
    tip(drag,L"Hold and drag the video into a chat, email or upload box.");
    ShowWindow(shareWindow,SW_SHOWNOACTIVATE);
    toastUntil=GetTickCount64()+12000; SetTimer(mainWindow,ToastTimer,500,nullptr);
}

// ---------- settings ----------
void settings() {
    if(state!=State::Idle) return;
    if(settingsWindow) { SetForegroundWindow(settingsWindow); return; }
    settingsWindow=CreateWindowExW(WS_EX_TOOLWINDOW,L"GlimpsePanel",L"Glimpse settings",
        WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_CLIPCHILDREN,CW_USEDEFAULT,CW_USEDEFAULT,560,500,mainWindow,nullptr,instance,nullptr);
    themeWindow(settingsWindow);
    RECT r{0,0,scale(settingsWindow,544),scale(settingsWindow,488)};
    AdjustWindowRectExForDpi(&r,WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU,FALSE,WS_EX_TOOLWINDOW,GetDpiForWindow(settingsWindow));
    SetWindowPos(settingsWindow,nullptr,0,0,r.right-r.left,r.bottom-r.top,SWP_NOMOVE|SWP_NOZORDER);
    child(settingsWindow,L"STATIC",L"Capture folder",0,20,14,490,22);
    darkControl(child(settingsWindow,L"EDIT",config.folder.c_str(),310,20,38,398,30,ES_AUTOHSCROLL|WS_TABSTOP));
    button(settingsWindow,L"Browse…",Browse,426,36,98);
    child(settingsWindow,L"STATIC",L"Recording shortcut",0,20,84,210,22);
    auto hotkey=child(settingsWindow,HOTKEY_CLASSW,L"",311,244,80,280,28,WS_BORDER|WS_TABSTOP);
    SendMessageW(hotkey,HKM_SETHOTKEY,config.hotkey,0);
    child(settingsWindow,L"STATIC",L"Quality",0,20,120,200,22);
    auto presets=child(settingsWindow,L"COMBOBOX",L"",312,244,116,280,160,CBS_DROPDOWNLIST|WS_TABSTOP|WS_VSCROLL);
    for(auto s:{L"AI Share — 1080p, up to 30 FPS",L"High Quality — 4K, up to 60 FPS",L"Smallest File — 720p, 15 FPS"}) SendMessageW(presets,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(s));
    SendMessageW(presets,CB_SETCURSEL,int(config.preset),0); darkControl(presets);
    child(settingsWindow,L"STATIC",L"Frame rate limit",0,20,156,200,22);
    auto fps=child(settingsWindow,L"COMBOBOX",L"",313,244,152,280,140,CBS_DROPDOWNLIST|WS_TABSTOP|WS_VSCROLL);
    for(auto s:{L"Preset default",L"15 FPS",L"30 FPS",L"60 FPS (High Quality)"}) SendMessageW(fps,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(s));
    SendMessageW(fps,CB_SETCURSEL,config.fps==15?1:config.fps==30?2:config.fps==60?3:0,0); darkControl(fps);
    child(settingsWindow,L"STATIC",L"Audio sources",0,20,192,200,22);
    auto audio=child(settingsWindow,L"COMBOBOX",L"",316,244,188,280,160,CBS_DROPDOWNLIST|WS_TABSTOP|WS_VSCROLL);
    for(auto s:{L"Silent",L"Microphone narration",L"System audio",L"Microphone + system audio"}) SendMessageW(audio,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(s));
    SendMessageW(audio,CB_SETCURSEL,config.audio,0); darkControl(audio);
    child(settingsWindow,L"STATIC",L"Microphone volume",0,20,230,210,22);
    slider(settingsWindow,317,244,226,220,config.micVolume,L"Default microphone volume");
    child(settingsWindow,L"STATIC",(std::to_wstring(config.micVolume)+L"%").c_str(),319,474,230,50,22);
    child(settingsWindow,L"STATIC",L"System audio volume",0,20,266,210,22);
    slider(settingsWindow,318,244,262,220,config.systemVolume,L"Default system audio volume");
    child(settingsWindow,L"STATIC",(std::to_wstring(config.systemVolume)+L"%").c_str(),320,474,266,50,22);
    auto check=[&](int id,const wchar_t* text,int y,bool value) {
        auto c=child(settingsWindow,L"BUTTON",text,id,20,y,504,26,BS_AUTOCHECKBOX|WS_TABSTOP);
        if(dark) SetWindowTheme(c,L"",L"");
        SendMessageW(c,BM_SETCHECK,value?BST_CHECKED:BST_UNCHECKED,0); return c;
    };
    check(314,L"Start in the tray when I sign in",304,config.startup);
    check(315,L"Use software encoding (compatibility mode)",334,config.software);
    tip(check(321,L"Save a diagnostics file next to each recording",364,config.diagnostics),
        L"Writes a small .metrics.txt file with timing and audio counters. Only useful for troubleshooting.");
    child(settingsWindow,L"STATIC",L"Uses your Windows default microphone and speakers.\nKeep system audio lower for clearer narration.",0,20,398,504,38);
    button(settingsWindow,L"Save",Save,312,440,112); button(settingsWindow,L"Cancel",Cancel,436,440,88);
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
    next.diagnostics=SendDlgItemMessageW(settingsWindow,321,BM_GETCHECK,0,0)==BST_CHECKED;
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
        setStartup(next.startup); writeConfig(next); config=next; updateTrayTip();
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
    auto newText=L"New recording\t"+hotkeyText(config.hotkey);
    AppendMenuW(menu,MF_STRING|(state==State::Idle?0:MF_GRAYED),New,newText.c_str());
    HMENU audio=CreatePopupMenu();
    const wchar_t* modes[]{L"Silent",L"Microphone narration",L"System audio",L"Microphone + system audio"};
    for(unsigned i=0;i<4;++i) AppendMenuW(audio,MF_STRING|(config.audio==i?MF_CHECKED:0),AudioMenuBase+i,modes[i]);
    AppendMenuW(menu,MF_POPUP|(state==State::Idle?0:MF_GRAYED),reinterpret_cast<UINT_PTR>(audio),L"Audio for next recording");
    if(state==State::Recording||state==State::Paused||state==State::Starting) AppendMenuW(menu,MF_STRING,Record,L"Stop and save");
    AppendMenuW(menu,MF_SEPARATOR,0,nullptr);
    AppendMenuW(menu,MF_POPUP,reinterpret_cast<UINT_PTR>(recent),L"Recent captures");
    AppendMenuW(menu,MF_STRING,Folder,L"Open captures folder");
    AppendMenuW(menu,MF_STRING|(state==State::Idle?0:MF_GRAYED),Settings,L"Settings…");
    AppendMenuW(menu,MF_SEPARATOR,0,nullptr); AppendMenuW(menu,MF_STRING,Exit,L"Exit");
    POINT p; GetCursorPos(&p); SetForegroundWindow(mainWindow);
    UINT cmd=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,p.x,p.y,0,mainWindow,nullptr);
    DestroyMenu(menu); PostMessageW(mainWindow,WM_NULL,0,0);
    if(cmd) PostMessageW(mainWindow,WM_COMMAND,cmd,0);
}
void handleCommand(HWND w,UINT id) {
    if(id>=AudioMenuBase&&id<AudioMenuBase+4&&state==State::Idle) {
        Config next=config; next.audio=id-AudioMenuBase; writeConfig(next); config=next; return;
    }
    if(id>=RecentBase&&id<RecentBase+recentFiles.size()) {
        KillTimer(mainWindow,RetryCopy); clipboardPending=false; clipboardCopied=false;
        latestFile=recentFiles[id-RecentBase]; shareSummary=latestFile.filename().wstring()+L"\nSaved capture";
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
            if(!p) animationStart=GetTickCount64();
            refreshIndicators();
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

// ---------- panel painting ----------
std::wstring statusText() {
    if(!statusOverride.empty()) return statusOverride;
    if(state==State::Starting||state==State::Ready) return L"Starting…";
    int seconds=int(recorder.elapsed()); wchar_t time[32];
    if(seconds>=3600) swprintf_s(time,L"%d:%02d:%02d",seconds/3600,seconds/60%60,seconds%60);
    else swprintf_s(time,L"%02d:%02d",seconds/60,seconds%60);
    return std::wstring(state==State::Paused?L"Paused   ":L"REC   ")+time;
}
void paintPanel(HWND w,HDC dc,const RECT& client) {
    FillRect(dc,&client,background);
    const float W=float(client.right), H=float(client.bottom);
    {
        gp::Graphics g(dc); smooth(g);
        if(!win11 && w!=settingsWindow) strokeRound(g,outline,255,gp::RectF(.5f,.5f,W-1,H-1),scalef(w,12.f),1.f);
        if(w==toolbar) {
            RECT s=statusRect(w); float cy=(s.top+s.bottom)/2.f, cx=float(s.left)+scalef(w,10.f);
            COLORREF c=state==State::Saving?Accent:stateColor();
            if(state==State::Recording) fillCircle(g,c,BYTE(60*pulse()),cx,cy,scalef(w,9.f));
            fillCircle(g,c,255,cx,cy,scalef(w,5.f));
            if(audioArmed) {
                gp::Pen line(color(outline),1.f);
                g.DrawLine(&line,scalef(w,12.f),float(scale(w,ToolbarLayout::Row)),W-scalef(w,12.f),float(scale(w,ToolbarLayout::Row)));
                bool live=state==State::Recording;
                for(int source=0;source<2;++source) {
                    auto& slot=audioSlots[source];
                    bool on=liveAudio&(source?AudioSystem:AudioMic);
                    float level=live&&on?std::clamp(source?recorder.systemLevel():recorder.micLevel(),0.f,1.f):0.f;
                    gp::RectF track(scalef(w,float(slot.meter)),scalef(w,ToolbarLayout::Row+36.f),scalef(w,float(MeterWidth)),scalef(w,3.f));
                    fillRound(g,outline,255,track,track.Height/2);
                    if(level>0.005f) {
                        // Perceptual (square-root) scale so normal speech fills a useful part of the bar.
                        gp::RectF fill=track; fill.Width=std::max(track.Height,track.Width*std::sqrt(level));
                        fillRound(g,level>.95f?RecordRed:level>.7f?Amber:Success,255,fill,track.Height/2);
                    }
                }
            }
        }
        if(w==shareWindow) {
            float cx=scalef(w,30.f), cy=scalef(w,28.f), r=scalef(w,11.f);
            fillCircle(g,Success,255,cx,cy,r);
            gp::Pen tick(color(White),scalef(w,2.f)); tick.SetStartCap(gp::LineCapRound); tick.SetEndCap(gp::LineCapRound); tick.SetLineJoin(gp::LineJoinRound);
            gp::PointF points[]{{cx-r*.45f,cy+r*.02f},{cx-r*.1f,cy+r*.36f},{cx+r*.48f,cy-r*.3f}};
            g.DrawLines(&tick,points,3);
        }
    }
    if(w==toolbar) {
        RECT s=statusRect(w); s.left+=scale(w,24);
        drawText(dc,propFont(w,L"GlimpseBold"),state==State::Paused?Amber:fg,statusText(),s,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
        if(audioArmed) for(int source=0;source<2;++source) {
            auto& slot=audioSlots[source]; int value=source?liveSystemVolume:liveMicVolume;
            RECT pct{scale(w,slot.percent),scale(w,ToolbarLayout::Row+6),scale(w,slot.percent+40),scale(w,ToolbarLayout::Row+40)};
            drawText(dc,propFont(w,L"GlimpseSmall"),muted,std::to_wstring(value)+L"%",pct,DT_RIGHT|DT_VCENTER|DT_SINGLELINE);
        }
    }
    if(w==shareWindow) {
        RECT title{scale(w,52),scale(w,14),scale(w,ShareWidth-52),scale(w,42)};
        drawText(dc,propFont(w,L"GlimpseHeading"),fg,L"Capture saved",title,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
    }
}
wchar_t glyphFor(UINT id,bool on) {
    switch(id) {
    case Pause: return state==State::Paused?0xE768:0xE769;  // Play / Pause
    case Cancel: return 0xE74D;                             // Delete
    case Mic: return 0xE720;                                // Microphone
    case SystemAudio: return on?0xE767:0xE74F;              // Volume / Mute
    case Dismiss: return 0xE8BB;                            // ChromeClose
    case Copy: return 0xE8C8;                               // Copy
    case Reveal: return 0xE838;                             // FolderOpen
    case Drag: return 0xE72D;                               // Share
    }
    return 0;
}
void drawButton(HWND w,DRAWITEMSTRUCT* d) {
    const RECT rc=d->rcItem; const int bw=rc.right-rc.left, bh=rc.bottom-rc.top;
    HDC mem=CreateCompatibleDC(d->hDC); HBITMAP bitmap=CreateCompatibleBitmap(d->hDC,bw,bh); auto oldBitmap=SelectObject(mem,bitmap);
    RECT local{0,0,bw,bh}; FillRect(mem,&local,background);
    const UINT id=d->CtlID;
    const bool disabled=(d->itemState&ODS_DISABLED)!=0, pressed=(d->itemState&ODS_SELECTED)!=0;
    const bool hot=GetPropW(d->hwndItem,L"GlimpseHot")!=nullptr && !disabled;
    const bool focus=(d->itemState&ODS_FOCUS)!=0 && !(d->itemState&ODS_NOFOCUSRECT);
    const bool stop=id==Record, accent=id==Copy||id==Save, toggle=id==Mic||id==SystemAudio;
    const bool on=toggle&&(liveAudio&(id==Mic?AudioMic:AudioSystem));
    const bool iconOnly=id==Pause||id==Record||toggle||id==Dismiss||(id==Cancel&&w==toolbar);
    COLORREF fill=pressed?surfacePressed:hot?surfaceHot:surface; BYTE alpha=255;
    COLORREF ink=disabled?muted:fg;
    if(id==Dismiss&&!hot&&!pressed) alpha=0;
    if(stop) { fill=pressed?RecordPressed:hot?RecordHot:RecordRed; ink=White; if(disabled) alpha=110; }
    else if(accent) { fill=pressed?AccentPressed:hot?AccentHot:Accent; ink=White; if(disabled) alpha=110; }
    else if(toggle&&on&&!disabled) { fill=dark?(hot?RGB(76,62,132):RGB(64,52,116)):(hot?RGB(222,214,255):RGB(234,229,255)); ink=dark?RGB(214,204,255):AccentPressed; }
    else if(id==Cancel&&w==toolbar&&hot) ink=RecordRed;
    const float radius=iconOnly?scalef(w,10.f):scalef(w,8.f);
    {
        gp::Graphics g(mem); smooth(g);
        fillRound(g,fill,alpha,gp::RectF(0,0,float(bw),float(bh)),radius);
        if(focus) strokeRound(g,Accent,255,gp::RectF(1,1,float(bw-2),float(bh-2)),radius-1,scalef(w,1.5f));
        if(stop) { float s=scalef(w,12.f); fillRound(g,White,disabled?150:255,gp::RectF((bw-s)/2,(bh-s)/2,s,s),scalef(w,3.f)); }
    }
    const wchar_t glyph=(id==Cancel&&w!=toolbar)?0:glyphFor(id,on); // Settings' Cancel is text-only
    if(iconOnly) {
        if(glyph) {
            std::wstring g(1,glyph);
            drawText(mem,propFont(w,L"GlimpseIcons"),ink,g,local,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
            if(id==Mic&&!on) { // slashed microphone
                gp::Graphics g2(mem); smooth(g2); gp::Pen slash(color(ink),scalef(w,1.7f)); slash.SetStartCap(gp::LineCapRound); slash.SetEndCap(gp::LineCapRound);
                g2.DrawLine(&slash,bw*.3f,bh*.28f,bw*.7f,bh*.72f);
            }
        }
    } else {
        auto text=windowText(d->hwndItem); HFONT font=panelFont(w), icons=propFont(w,L"GlimpseIcons");
        std::wstring g=glyph?std::wstring(1,glyph):L"";
        int gw=glyph?textSize(mem,icons,g).cx:0, gap=glyph?scale(w,8):0, tw=textSize(mem,font,text).cx;
        int x=(bw-(gw+gap+tw))/2;
        if(glyph) drawText(mem,icons,ink,g,{x,0,x+gw,bh},DT_LEFT|DT_VCENTER|DT_SINGLELINE);
        drawText(mem,font,ink,text,{x+gw+gap,0,bw,bh},DT_LEFT|DT_VCENTER|DT_SINGLELINE);
    }
    BitBlt(d->hDC,rc.left,rc.top,bw,bh,mem,0,0,SRCCOPY);
    SelectObject(mem,oldBitmap); DeleteObject(bitmap); DeleteDC(mem);
}
LRESULT drawSlider(HWND w,NMCUSTOMDRAW* draw) {
    if(draw->dwDrawStage==CDDS_PREPAINT) return CDRF_NOTIFYITEMDRAW;
    if(draw->dwDrawStage!=CDDS_ITEMPREPAINT) return CDRF_DODEFAULT;
    HWND control=draw->hdr.hwndFrom; bool enabled=IsWindowEnabled(control)!=FALSE;
    if(draw->dwItemSpec==TBCD_TICS) return CDRF_SKIPDEFAULT;
    if(draw->dwItemSpec==TBCD_CHANNEL) {
        RECT r=draw->rc; float h=scalef(w,4.f), cy=(r.top+r.bottom)/2.f;
        gp::Graphics g(draw->hdc); smooth(g);
        gp::RectF track(float(r.left),cy-h/2,float(r.right-r.left),h);
        fillRound(g,outline,255,track,h/2);
        RECT thumb{}; SendMessageW(control,TBM_GETTHUMBRECT,0,reinterpret_cast<LPARAM>(&thumb));
        gp::RectF fill=track; fill.Width=std::max(h,(thumb.left+thumb.right)/2.f-track.X);
        fillRound(g,enabled?Accent:muted,255,fill,h/2);
        return CDRF_SKIPDEFAULT;
    }
    if(draw->dwItemSpec==TBCD_THUMB) {
        RECT r=draw->rc; float cx=(r.left+r.right)/2.f, cy=(r.top+r.bottom)/2.f, radius=scalef(w,7.f);
        gp::Graphics g(draw->hdc); smooth(g);
        fillCircle(g,enabled?Accent:muted,255,cx,cy,radius);
        fillCircle(g,White,255,cx,cy,radius-scalef(w,2.5f));
        if(GetFocus()==control) { gp::Pen ring(color(Accent,120),scalef(w,2.f)); g.DrawEllipse(&ring,cx-radius-2,cy-radius-2,radius*2+4,radius*2+4); }
        return CDRF_SKIPDEFAULT;
    }
    return CDRF_DODEFAULT;
}
LRESULT CALLBACK panelProc(HWND w,UINT msg,WPARAM wp,LPARAM lp) {
    switch(msg) {
    case WM_COMMAND: handleCommand(w,LOWORD(wp)); return 0;
    case WM_NOTIFY: {
        auto header=reinterpret_cast<NMHDR*>(lp);
        if(header->code!=NM_CUSTOMDRAW) break;
        auto id=header->idFrom;
        if(id!=MicVolume&&id!=SystemVolume&&id!=317&&id!=318) break;
        return drawSlider(w,reinterpret_cast<NMCUSTOMDRAW*>(lp));
    }
    case WM_HSCROLL:
        if(w==toolbar) {
            if(micSlider) liveMicVolume=int(SendMessageW(micSlider,TBM_GETPOS,0,0));
            if(systemSlider) liveSystemVolume=int(SendMessageW(systemSlider,TBM_GETPOS,0,0));
            audioControls();
        } else if(w==settingsWindow) {
            for(auto pair:{std::pair{317,319},std::pair{318,320}}) {
                auto value=SendDlgItemMessageW(w,pair.first,TBM_GETPOS,0,0);
                SetDlgItemTextW(w,pair.second,(std::to_wstring(value)+L"%").c_str());
            }
        }
        if(lp) InvalidateRect(reinterpret_cast<HWND>(lp),nullptr,FALSE);
        return 0;
    case WM_CLOSE:
        if(w==toolbar) stopRecording();
        else { if(w==settingsWindow) settingsWindow=nullptr; if(w==shareWindow) shareWindow=nullptr; DestroyWindow(w); }
        return 0;
    case WM_NCHITTEST: {
        auto hit=DefWindowProcW(w,msg,wp,lp);
        if(hit==HTCLIENT && w!=settingsWindow) return HTCAPTION; return hit;
    }
    case WM_NCLBUTTONDBLCLK: if(w!=settingsWindow) return 0; break; // never maximize a popup panel
    case WM_MOUSEACTIVATE: if(w==shareWindow) return MA_NOACTIVATE; break;
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
        SetPropW(w,L"GlimpseDpi",reinterpret_cast<HANDLE>(UINT_PTR(newDpi)));
        InvalidateRect(w,nullptr,TRUE); return 0;
    }
    case WM_CREATE:
        SetPropW(w,L"GlimpseDpi",reinterpret_cast<HANDLE>(UINT_PTR(GetDpiForWindow(w))));
        renewFont(w,GetDpiForWindow(w)); return 0;
    case WM_NCDESTROY:
        dropFonts(w); RemovePropW(w,L"GlimpseDpi"); break;
    case WM_SIZE:
        // Windows 11 rounds popups itself (anti-aliased, with a shadow); Windows 10 needs a region.
        if(!win11 && (GetWindowLongPtrW(w,GWL_STYLE)&WS_CAPTION)!=WS_CAPTION) {
            RECT r; GetWindowRect(w,&r);
            HRGN region=CreateRoundRectRgn(0,0,r.right-r.left+1,r.bottom-r.top+1,scale(w,24),scale(w,24));
            if(!SetWindowRgn(w,region,TRUE)) DeleteObject(region);
        } return 0;
    case WM_CTLCOLORSTATIC: case WM_CTLCOLORBTN: {
        HDC dc=reinterpret_cast<HDC>(wp); SetTextColor(dc,GetDlgCtrlID(reinterpret_cast<HWND>(lp))==215?muted:fg);
        SetBkColor(dc,bg); return reinterpret_cast<LRESULT>(background);
    }
    case WM_CTLCOLOREDIT: case WM_CTLCOLORLISTBOX: {
        HDC dc=reinterpret_cast<HDC>(wp); SetTextColor(dc,fg); SetBkColor(dc,surface); return reinterpret_cast<LRESULT>(fieldBrush);
    }
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC dc=BeginPaint(w,&ps); RECT r; GetClientRect(w,&r);
        HDC buffer{}; HPAINTBUFFER pb=BeginBufferedPaint(dc,&r,BPBF_COMPATIBLEBITMAP,nullptr,&buffer);
        if(pb) { paintPanel(w,buffer,r); EndBufferedPaint(pb,TRUE); } else paintPanel(w,dc,r);
        EndPaint(w,&ps); return 0;
    }
    case WM_DRAWITEM: drawButton(w,reinterpret_cast<DRAWITEMSTRUCT*>(lp)); return TRUE;
    }
    return DefWindowProcW(w,msg,wp,lp);
}
LRESULT CALLBACK frameProc(HWND w,UINT msg,WPARAM wp,LPARAM lp) {
    if(msg==WM_NCHITTEST) return HTTRANSPARENT;
    if(msg==WM_ERASEBKGND) return 1;
    if(msg==WM_PAINT) {
        PAINTSTRUCT ps; HDC dc=BeginPaint(w,&ps);
        if(w==recordingFrame) {
            RECT r; GetClientRect(w,&r);
            HBRUSH brush=CreateSolidBrush(stateColor()); FillRect(dc,&r,brush); DeleteObject(brush);
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
        if(state==State::Starting) {
            state=State::Recording; animationStart=GetTickCount64();
            EnableWindow(pauseButton,TRUE); audioControls(); refreshIndicators();
        }
        return 0;
    case WM_CAPTURE_FINISHED: {
        std::unique_ptr<CaptureResult> result(reinterpret_cast<CaptureResult*>(lp)); recorder.join(); closeSelection();
        if(!result->file.empty()) {
            latestFile=result->file;
            wchar_t detail[128]; swprintf_s(detail,L"%.1f MB  ·  %.1f s  ·  %s",double(result->bytes)/1048576.,result->seconds,audioArmed?L"with audio":L"no audio");
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
            // 20 Hz: smooth level meters and pulse; only small, double-buffered areas are repainted.
            if(state==State::Recording) updateDot();
            invalidateToolbar(true,state==State::Recording);
        } else if(wp==RetryCopy) {
            bool copied=copyFile();
            if(copied||++copyAttempts>=20) {
                KillTimer(w,RetryCopy); clipboardPending=false; clipboardCopied=copied;
                auto summary=shareSummary+(copied?L"\nCopied — paste it anywhere that accepts files.":L"\nClipboard busy — use Copy file or Drag.");
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
    gp::GdiplusStartupInput gdiplusInput; gp::GdiplusStartup(&gdiplusToken,&gdiplusInput,nullptr);
    BufferedPaintInit();
    try {
        // VersionHelpers needs a manifest; verify the actual build before relying on capture exclusion.
        using VersionFn=LONG(WINAPI*)(OSVERSIONINFOW*);
        auto version=reinterpret_cast<VersionFn>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"),"RtlGetVersion"));
        OSVERSIONINFOW os{sizeof(os)};
        if(!version||version(&os)!=0||os.dwBuildNumber<19041) throw winrt::hresult_error(E_NOTIMPL,L"Glimpse requires Windows 10 version 2004 or newer, or Windows 11.");
        win11=os.dwBuildNumber>=22000;
        fontFace=win11?L"Segoe UI Variable Text":L"Segoe UI";
        iconFace=win11?L"Segoe Fluent Icons":L"Segoe MDL2 Assets";
        INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_STANDARD_CLASSES|ICC_WIN95_CLASSES}; InitCommonControlsEx(&controls);
        DWORD light=1, bytes=sizeof(light);
        RegGetValueW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",L"AppsUseLightTheme",RRF_RT_REG_DWORD,nullptr,&light,&bytes);
        dark=light==0;
        bg=dark?RGB(32,32,38):RGB(250,250,252); fg=dark?RGB(242,242,247):RGB(24,24,30);
        muted=dark?RGB(158,158,172):RGB(104,104,118);
        surface=dark?RGB(46,46,55):RGB(237,237,243); surfaceHot=dark?RGB(58,58,69):RGB(226,226,235);
        surfacePressed=dark?RGB(70,70,83):RGB(214,214,226); outline=dark?RGB(62,62,74):RGB(222,222,232);
        background=CreateSolidBrush(bg); fieldBrush=CreateSolidBrush(surface);
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
    if(uiFont) DeleteObject(uiFont);
    if(background) DeleteObject(background);
    if(fieldBrush) DeleteObject(fieldBrush);
    BufferedPaintUnInit(); gp::GdiplusShutdown(gdiplusToken);
    OleUninitialize(); CloseHandle(single); return 0;
}
