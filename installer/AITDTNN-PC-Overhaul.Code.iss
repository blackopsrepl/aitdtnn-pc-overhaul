{ Wizard and install-transaction implementation. }
{ AITDTNN-PC-Overhaul.iss contains the declarative package surface. }

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
  { The manager dot-sources its filesystem safety helpers from the same folder. }
  if not FileExists(ExpandConstant('{tmp}\Manage-Overhaul.Core.ps1')) then
    ExtractTemporaryFile('Manage-Overhaul.Core.ps1');
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
      MsgBox('This combined overhaul supports only the verified English 15-slot/no-CD or retail CD alone4.exe.',
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
