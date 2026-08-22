#ifndef MyAppVersion
  #define MyAppVersion "0.5.1"
#endif

[Setup]
AppId={{3BE460B0-C967-4F4F-838B-9520E93DBE05}
AppName=AITD:TNN PC Overhaul
AppVersion={#MyAppVersion}
AppVerName=AITD:TNN PC Overhaul {#MyAppVersion}
AppPublisher=AITD:TNN PC Overhaul contributors
VersionInfoVersion={#MyAppVersion}
DefaultDirName={code:GetOverhaulDir}
DisableDirPage=yes
OutputDir=..\build\release
OutputBaseFilename=AITDTNN-PC-Overhaul-Setup-{#MyAppVersion}
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
ArchitecturesAllowed=x64compatible
UsePreviousAppDir=no
SetupLogging=yes
CloseApplications=yes
CloseApplicationsFilter=alone4.exe
Uninstallable=yes
UninstallFilesDir={app}
UninstallDisplayName=AITD:TNN PC Overhaul {#MyAppVersion}
UninstallDisplayIcon={code:GetGameExe}
LicenseFile=..\LICENSE.txt

[Files]
Source: "..\payload\game\version.dll"; DestDir: "{code:GetGameDir}"; Flags: ignoreversion uninsneveruninstall
Source: "..\payload\game\audio-restoration\aitd4-audio-hook.dll"; DestDir: "{code:GetGameDir}\audio-restoration"; Flags: ignoreversion uninsneveruninstall
Source: "..\audio-restoration\bin\aitdtnn-assets.exe"; Flags: dontcopy
Source: "..\audio-restoration\bin\aitdtnn-assets.exe"; DestDir: "{app}\tools"; Flags: ignoreversion uninsneveruninstall
Source: "{tmp}\aitdtnn-overhaul-runtime-assets\*"; DestDir: "{code:GetGameDir}\audio-restoration\runtime-assets"; Flags: external recursesubdirs createallsubdirs ignoreversion uninsneveruninstall
Source: "..\payload\game\renderer\aitd4-renderer-hook.dll"; DestDir: "{code:GetGameDir}\renderer"; Flags: ignoreversion uninsneveruninstall
Source: "..\renderer\config\aitd4-overhaul.renderer.ini"; DestDir: "{code:GetGameDir}\renderer"; DestName: "aitd4-overhaul.ini"; Flags: ignoreversion uninsneveruninstall
Source: "..\renderer\shaders\compositor.vert"; DestDir: "{code:GetGameDir}\renderer\shaders"; Flags: ignoreversion uninsneveruninstall
Source: "..\renderer\shaders\compositor.frag"; DestDir: "{code:GetGameDir}\renderer\shaders"; Flags: ignoreversion uninsneveruninstall
Source: "..\renderer\shaders\crt_signal.frag"; DestDir: "{code:GetGameDir}\renderer\shaders"; Flags: ignoreversion uninsneveruninstall
Source: "..\renderer\shaders\crt_response.frag"; DestDir: "{code:GetGameDir}\renderer\shaders"; Flags: ignoreversion uninsneveruninstall
Source: "..\renderer\shaders\crt_blur.frag"; DestDir: "{code:GetGameDir}\renderer\shaders"; Flags: ignoreversion uninsneveruninstall
Source: "..\renderer\shaders\crt_present.frag"; DestDir: "{code:GetGameDir}\renderer\shaders"; Flags: ignoreversion uninsneveruninstall
Source: "..\payload\game\rumble\aitd4-rumble-hook.dll"; DestDir: "{code:GetGameDir}\rumble"; Flags: ignoreversion uninsneveruninstall
Source: "..\rumble\config\aitd4-rumble.ini"; DestDir: "{code:GetGameDir}\rumble"; Flags: ignoreversion uninsneveruninstall
Source: "..\payload\game\dinput8.dll"; DestDir: "{code:GetGameDir}"; Flags: ignoreversion uninsneveruninstall
Source: "..\payload\game\winmm.dll"; DestDir: "{code:GetGameDir}"; Flags: ignoreversion uninsneveruninstall
Source: "..\payload\game\Xidi.32.dll"; DestDir: "{code:GetGameDir}"; Flags: ignoreversion uninsneveruninstall
Source: "..\payload\game\Xidi.ini"; DestDir: "{code:GetGameDir}"; Flags: ignoreversion uninsneveruninstall
Source: "..\payload\game\keys.bin"; DestDir: "{code:GetGameDir}"; Flags: ignoreversion uninsneveruninstall
Source: "Manage-Overhaul.ps1"; Flags: dontcopy
Source: "Manage-Overhaul.Core.ps1"; Flags: dontcopy
Source: "Manage-Overhaul.ps1"; DestDir: "{app}\tools"; Flags: ignoreversion uninsneveruninstall
Source: "Manage-Overhaul.Core.ps1"; DestDir: "{app}\tools"; Flags: ignoreversion uninsneveruninstall
Source: "..\README.md"; DestDir: "{app}"; Flags: ignoreversion uninsneveruninstall
Source: "..\REPORT.md"; DestDir: "{app}"; Flags: ignoreversion uninsneveruninstall
Source: "..\docs\*.md"; DestDir: "{app}\docs"; Flags: ignoreversion uninsneveruninstall
Source: "..\NOTICE.md"; DestDir: "{app}"; Flags: ignoreversion uninsneveruninstall
Source: "..\LICENSE.txt"; DestDir: "{app}"; Flags: ignoreversion uninsneveruninstall
Source: "..\renderer\LICENSE.txt"; DestDir: "{app}\licenses"; DestName: "renderer-MIT.txt"; Flags: ignoreversion uninsneveruninstall
Source: "..\renderer\THIRD_PARTY.md"; DestDir: "{app}\licenses"; DestName: "renderer-THIRD-PARTY.md"; Flags: ignoreversion uninsneveruninstall
Source: "..\audio-restoration\THIRD_PARTY.md"; DestDir: "{app}\licenses"; Flags: ignoreversion uninsneveruninstall
Source: "..\audio-restoration\licenses\*"; DestDir: "{app}\licenses\audio-restoration"; Flags: ignoreversion recursesubdirs createallsubdirs uninsneveruninstall
Source: "..\third_party\Xidi-v5.0.0\LICENSE"; DestDir: "{app}\licenses"; DestName: "Xidi-LICENSE.txt"; Flags: ignoreversion uninsneveruninstall
Source: "..\third_party\Xidi-v5.0.0\README.md"; DestDir: "{app}\licenses"; DestName: "Xidi-README.md"; Flags: ignoreversion uninsneveruninstall

[Code]
const
  Supported15SlotExeHash = '5668118E0E19D569986500A1C805A85397C8681E7B672B49A68645462ECCC672';
  SupportedRetailExeHash = '320908AF4CE5C724B60A7EEA6A5AADE737D51D65AEE8506744FCE6E6DD0143E0';

var
  GamePage: TInputDirWizardPage;
  ImagePage: TInputFileWizardPage;
  GeneratedAssets: String;
  BackupPath: String;
  BeginSucceeded: Boolean;
  FinalizeSucceeded: Boolean;
  InstallFinished: Boolean;


#include "AITDTNN-PC-Overhaul.Code.iss"
