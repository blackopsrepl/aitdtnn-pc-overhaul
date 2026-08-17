@echo off
setlocal
cd /d "%~dp0"
call "..\shared\tools\vcvars32.cmd"
if errorlevel 1 exit /b %errorlevel%
if not exist build mkdir build
if not exist bin mkdir bin

cl /nologo /Brepro /std:c++20 /O2 /W4 /WX /EHsc /MT /I src ^
  /Fo:build\rumble_protocol_test.obj /Fe:build\rumble-protocol-test.exe ^
  tests\rumble_protocol_test.cpp /link /Brepro
if errorlevel 1 exit /b %errorlevel%

cl /nologo /c /Brepro /std:c++20 /O2 /W4 /WX /EHsc- /GR- /MT /DWIN32 /DWIN32_LEAN_AND_MEAN /DNOMINMAX ^
  /Fo:build\hook_main.obj src\hook_main.cpp
if errorlevel 1 exit /b %errorlevel%

cl /nologo /c /Brepro /std:c++17 /O2 /W4 /WX /EHsc- /GR- /MT /DWIN32 /DWIN32_LEAN_AND_MEAN /DNOMINMAX ^
  /Fo:build\sha256.obj ..\shared\loader\src\sha256.cpp
if errorlevel 1 exit /b %errorlevel%

link /nologo /Brepro /DLL /MACHINE:X86 /DYNAMICBASE /NXCOMPAT /OPT:REF /OPT:ICF /INCREMENTAL:NO ^
  /DEF:src\hook.def /OUT:bin\aitd4-rumble-hook.dll ^
  build\hook_main.obj build\sha256.obj kernel32.lib user32.lib
if errorlevel 1 exit /b %errorlevel%

build\rumble-protocol-test.exe
set test_result=%errorlevel%
if not "%test_result%"=="0" exit /b %test_result%

dumpbin /exports bin\aitd4-rumble-hook.dll | findstr /R /C:"AITD4_RumbleInitialize$" >nul
if errorlevel 1 exit /b 90
dumpbin /exports bin\aitd4-rumble-hook.dll | findstr /C:"_AITD4_RumbleInitialize@" >nul
if not errorlevel 1 exit /b 91

dumpbin /dependents bin\aitd4-rumble-hook.dll | findstr /I /C:"MSVCP" /C:"VCRUNTIME" /C:"UCRTBASE" >nul
if not errorlevel 1 exit /b 92
exit /b 0
