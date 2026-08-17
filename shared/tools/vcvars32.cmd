@echo off
setlocal EnableExtensions EnableDelayedExpansion

if defined VSCMD_ARG_TGT_ARCH (
  endlocal & exit /b 0
)

set "AITD4_VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "!AITD4_VSWHERE!" (
  echo Visual Studio Installer\vswhere.exe was not found. 1>&2
  endlocal & exit /b 2
)

set "AITD4_VSROOT="
for /f "usebackq delims=" %%I in (`"!AITD4_VSWHERE!" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "AITD4_VSROOT=%%I"
if not defined AITD4_VSROOT (
  echo Visual Studio with the x86 C++ toolchain was not found. 1>&2
  endlocal & exit /b 3
)

set "AITD4_VCVARS=!AITD4_VSROOT!\VC\Auxiliary\Build\vcvars32.bat"
if not exist "!AITD4_VCVARS!" (
  echo vcvars32.bat was not found under !AITD4_VSROOT!. 1>&2
  endlocal & exit /b 4
)

for %%I in ("!AITD4_VCVARS!") do endlocal & call "%%~fI" >nul
exit /b %errorlevel%
