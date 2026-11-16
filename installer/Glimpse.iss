; Glimpse installer (Inno Setup 6). Built by .github/workflows/release.yml:
;   ISCC /DAppVersion=1.2.3 /DStageDir=<cmake --install output> /DOutputDir=<folder> installer\Glimpse.iss
; Installs per user (no administrator prompt) to %LOCALAPPDATA%\Programs\Glimpse.
; Running a newer Setup over an existing install updates it in place.

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef StageDir
  #define StageDir "..\stage"
#endif
#ifndef OutputDir
  #define OutputDir "..\dist"
#endif

[Setup]
AppId={{8F3C2B1A-5D47-4E8B-9A61-3C7E2D9F4B10}
AppName=Glimpse
AppVersion={#AppVersion}
AppVerName=Glimpse {#AppVersion}
AppPublisher=Noisewater Labs
AppPublisherURL=https://github.com/DrNoisewater/Glimpse
AppSupportURL=https://github.com/DrNoisewater/Glimpse/issues
AppUpdatesURL=https://github.com/DrNoisewater/Glimpse/releases
VersionInfoVersion={#AppVersion}
DefaultDirName={autopf}\Glimpse
DisableProgramGroupPage=yes
DisableDirPage=auto
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.19041
; Glimpse holds this mutex while running; Setup asks you to close it first.
AppMutex=Local\Glimpse.ScreenRecorder.v1
SetupIconFile=..\src\glimpse.ico
UninstallDisplayIcon={app}\Glimpse.exe
UninstallDisplayName=Glimpse
WizardStyle=modern
Compression=lzma2/max
SolidCompression=yes
OutputDir={#OutputDir}
OutputBaseFilename=Glimpse-Setup-{#AppVersion}

[Tasks]
Name: "startup"; Description: "Start Glimpse in the tray when I sign in"; Flags: unchecked
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "{#StageDir}\Glimpse.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#StageDir}\README.md"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\Glimpse"; Filename: "{app}\Glimpse.exe"; Comment: "Record an area of your screen"
Name: "{autodesktop}\Glimpse"; Filename: "{app}\Glimpse.exe"; Tasks: desktopicon

[Registry]
; Same value the app's own "Start in the tray when I sign in" setting uses.
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: string; ValueName: "Glimpse"; ValueData: """{app}\Glimpse.exe"""; Tasks: startup
; Remove that value on uninstall whichever way it was turned on.
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: none; ValueName: "Glimpse"; Flags: uninsdeletevalue dontcreatekey

[Run]
Filename: "{app}\Glimpse.exe"; Description: "Start Glimpse now"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
; Settings only. Recordings in Videos\Glimpse are never touched.
Type: filesandordirs; Name: "{localappdata}\Glimpse"
