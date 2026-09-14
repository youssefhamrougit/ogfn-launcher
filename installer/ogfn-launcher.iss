; ogfn-launcher.iss — Inno Setup script for OGFN Launcher.
; Compile with ISCC.exe (or `cmake --build build --target installer` when
; Inno Setup 6 is installed at the default location).
;
; The script expects a Release build to exist at ..\build\Release\OGFNLauncher.exe
; (build it with: cmake --build build --config Release)

#define MyAppName "OGFN Launcher"
#define MyAppVersion "0.1.0"
#define MyAppPublisher "OGFN"
#define MyAppExeName "OGFNLauncher.exe"

[Setup]
AppId={{8E5B2C64-1D2A-4B7E-9F0C-3A7D5E1B9C42}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
DefaultDirName={autopf}\OGFN Launcher
DefaultGroupName=OGFN Launcher
DisableProgramGroupPage=yes
OutputDir=out
OutputBaseFilename=OGFNLauncher-Setup-{#MyAppVersion}
SetupIconFile=app.ico
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
PrivilegesRequired=admin
ArchitecturesInstallIn64BitMode=x64compatible
UninstallDisplayIcon={app}\{#MyAppExeName}
MinVersion=6.2

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: checkedonce

[Files]
; Main executable (web\ assets are copied next to it by the CMake post-build step).
Source: "..\build\Release\OGFNLauncher.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\build\Release\web\*"; DestDir: "{app}\web"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#MyAppName}}}"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
; WebView2 user-data folder lives in %APPDATA%, so nothing to clean in {app}.
Type: filesandordirs; Name: "{app}\web"
