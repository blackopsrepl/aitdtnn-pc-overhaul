#ifndef MyAppVersion
  #define MyAppVersion "0.3.0"
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
Source: "Manage-Overhaul.ps1"; DestDir: "{app}\tools"; Flags: ignoreversion uninsneveruninstall
Source: "..\README.md"; DestDir: "{app}"; Flags: ignoreversion uninsneveruninstall
Source: "..\REPORT.md"; DestDir: "{app}"; Flags: ignoreversion uninsneveruninstall
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

function DetectGameDirectory: String;
begin
  Result := '';
  RegQueryStringValue(HKCU, 'Software\Infogrames\Alone in the Dark\1.0',
    'Alone_Install', Result);
  if Result = '' then
    RegQueryStringValue(HKLM32, 'Software\Infogrames\Alone in the Dark\1.0',
      'Alone_Install', Result);
  if (Result = '') or not FileExists(AddBackslash(Result) + 'alone4.exe') then
    Result := ExpandConstant('{pf32}\Infogrames\Alone in the Dark - The New Nightmare');
end;

function GetGameDir(Param: String): String;
begin
  if Assigned(GamePage) then
    Result := RemoveBackslashUnlessRoot(GamePage.Values[0])
  else
    Result := DetectGameDirectory;
end;

function GetGameExe(Param: String): String;
begin
  Result := AddBackslash(GetGameDir('')) + 'alone4.exe';
end;

function GetOverhaulDir(Param: String): String;
begin
  Result := AddBackslash(GetGameDir('')) + 'aitdtnn-overhaul';
end;

function GetInstalledGameDir: String;
begin
  Result := ExtractFileDir(ExpandConstant('{app}'));
end;

function PowerShellPath: String;
begin
  Result := ExpandConstant('{sys}\WindowsPowerShell\v1.0\powershell.exe');
end;

function BuilderPath: String;
begin
  Result := ExpandConstant('{tmp}\aitdtnn-assets.exe');
end;

function ManagerPath: String;
begin
  Result := ExpandConstant('{tmp}\Manage-Overhaul.ps1');
end;

procedure EnsureTemporaryTools;
begin
  if not FileExists(BuilderPath) then
    ExtractTemporaryFile('aitdtnn-assets.exe');
  if not FileExists(ManagerPath) then
    ExtractTemporaryFile('Manage-Overhaul.ps1');
end;

function RunManager(Manager: String; Mode: String; GameDir: String;
  AppDir: String; Backup: String; var ResultCode: Integer): Boolean;
var
  Parameters: String;
begin
  Parameters := '-NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File ' +
    AddQuotes(Manager) + ' -Mode ' + Mode + ' -GamePath ' + AddQuotes(GameDir) +
    ' -AppPath ' + AddQuotes(AppDir);
  if Backup <> '' then
    Parameters := Parameters + ' -BackupPath ' + AddQuotes(Backup);
  Result := Exec(PowerShellPath, Parameters, '', SW_HIDE,
    ewWaitUntilTerminated, ResultCode);
end;

function ExactExecutableSupported: Boolean;
begin
  Result := FileExists(GetGameExe('')) and
    ((CompareText(GetSHA256OfFile(GetGameExe('')), Supported15SlotExeHash) = 0) or
     (CompareText(GetSHA256OfFile(GetGameExe('')), SupportedRetailExeHash) = 0));
end;

procedure InitializeWizard;
var
  CommandLineGamePath: String;
  CommandLineImage: String;
begin
  GamePage := CreateInputDirPage(wpSelectDir,
    'Select the installed PC game',
    'Where is Alone in the Dark: The New Nightmare installed?',
    'Select the folder containing the supported alone4.exe, then click Next.', False, '');
  GamePage.Add('PC game folder:');
  CommandLineGamePath := ExpandConstant('{param:GAMEPATH|}');
  if CommandLineGamePath <> '' then
    GamePage.Values[0] := CommandLineGamePath
  else
    GamePage.Values[0] := DetectGameDirectory;
  WizardForm.DirEdit.Text := GetOverhaulDir('');

  ImagePage := CreateInputFilePage(GamePage.ID,
    'Select your Dreamcast disc image',
    'Choose either retail Dreamcast disc.',
    'Setup extracts only the required audio data locally. The image is never modified.');
  ImagePage.Add('Dreamcast image:',
    'Dreamcast images|*.cue;*.gdi;*.iso;*.bin;*.img;*.raw|All files|*.*',
    '.cue');
  CommandLineImage := ExpandConstant('{param:DREAMCASTIMAGE|}');
  if CommandLineImage <> '' then
    ImagePage.Values[0] := CommandLineImage;
end;

function NextButtonClick(CurPageID: Integer): Boolean;
begin
  Result := True;
  if CurPageID = GamePage.ID then
  begin
    WizardForm.DirEdit.Text := GetOverhaulDir('');
    if not FileExists(GetGameExe('')) then
    begin
      MsgBox('alone4.exe was not found in the selected folder.', mbError, MB_OK);
      Result := False;
    end
    else if not ExactExecutableSupported then
    begin
      MsgBox('This combined overhaul supports only the verified English 15-slot/no-CD alone4.exe.',
        mbError, MB_OK);
      Result := False;
    end;
  end
  else if CurPageID = ImagePage.ID then
  begin
    if not FileExists(ImagePage.Values[0]) then
    begin
      MsgBox('Select a valid Dreamcast image file.', mbError, MB_OK);
      Result := False;
    end;
  end;
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
var
  ResultCode: Integer;
  Parameters: String;
begin
  Result := '';
  EnsureTemporaryTools;
  WizardForm.DirEdit.Text := GetOverhaulDir('');
  if not ExactExecutableSupported then
  begin
    Result := 'The selected alone4.exe does not match the exact supported build.';
    exit;
  end;
  if not FileExists(ImagePage.Values[0]) then
  begin
    Result := 'The selected Dreamcast image file does not exist.';
    exit;
  end;

  GeneratedAssets := ExpandConstant('{tmp}\aitdtnn-overhaul-runtime-assets');
  if DirExists(GeneratedAssets) then
    DelTree(GeneratedAssets, True, True, True);
  ForceDirectories(GeneratedAssets);
  Parameters := AddQuotes(ImagePage.Values[0]) + ' ' + AddQuotes(GeneratedAssets);
  if not Exec(BuilderPath, Parameters, '', SW_HIDE, ewWaitUntilTerminated, ResultCode) then
    Result := 'The Dreamcast asset builder could not be started.'
  else if ResultCode <> 0 then
    Result := 'The selected image could not be read or does not contain the required Dreamcast audio data.'
  else if not FileExists(AddBackslash(GeneratedAssets) + 'asset-manifest.json') then
    Result := 'Asset extraction did not produce a valid runtime manifest.';
end;

procedure CurStepChanged(CurStep: TSetupStep);
var
  ResultCode: Integer;
begin
  if CurStep = ssInstall then
  begin
    EnsureTemporaryTools;
    BackupPath := AddBackslash(GetGameDir('')) + '.aitdtnn-overhaul-install-backup-' +
      GetDateTimeString('yyyymmdd-hhnnss', '-', '-');
    if not RunManager(ManagerPath, 'BeginInstall', GetGameDir(''),
      GetOverhaulDir(''), BackupPath, ResultCode) or (ResultCode <> 0) then
      RaiseException('The existing game stack could not be preserved. No overhaul files were installed.');
    BeginSucceeded := True;
  end
  else if CurStep = ssPostInstall then
  begin
    if not RunManager(ExpandConstant('{app}\tools\Manage-Overhaul.ps1'),
      'FinalizeInstall', GetGameDir(''), ExpandConstant('{app}'), BackupPath,
      ResultCode) or (ResultCode <> 0) then
      RaiseException('The installed overhaul failed final verification and will be rolled back.');
    FinalizeSucceeded := True;
  end
  else if CurStep = ssDone then
    InstallFinished := True;
end;

procedure DeinitializeSetup;
var
  ResultCode: Integer;
  Manager: String;
begin
  if BeginSucceeded and not InstallFinished then
  begin
    if FileExists(ExpandConstant('{app}\tools\Manage-Overhaul.ps1')) then
      Manager := ExpandConstant('{app}\tools\Manage-Overhaul.ps1')
    else
    begin
      EnsureTemporaryTools;
      Manager := ManagerPath;
    end;
    RunManager(Manager, 'Rollback', GetGameDir(''), GetOverhaulDir(''),
      BackupPath, ResultCode);
  end;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  ResultCode: Integer;
begin
  if CurUninstallStep = usUninstall then
  begin
    if not RunManager(ExpandConstant('{app}\tools\Manage-Overhaul.ps1'),
      'Uninstall', GetInstalledGameDir, ExpandConstant('{app}'), '', ResultCode) or
      (ResultCode <> 0) then
      RaiseException('Safe uninstall failed. The overhaul and its backups were left in place.');
  end;
end;
