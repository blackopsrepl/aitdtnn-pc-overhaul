@echo off
setlocal
cd /d "%~dp0"
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars32.bat" >nul
if errorlevel 1 exit /b %errorlevel%
if not exist build mkdir build
if not exist bin mkdir bin

cl /nologo /Brepro /std:c++20 /O2 /W4 /WX /EHsc /MT /DWIN32 /LD ^
  /Fo:build\bink_stub.obj /Fe:build\binkw32.dll tests\bink_stub.cpp ^
  /link /Brepro /INCREMENTAL:NO /IMPLIB:build\binkw32.lib
if errorlevel 1 exit /b %errorlevel%

cl /nologo /Brepro /std:c++20 /O2 /W4 /WX /EHsc /I src ^
  /Fo:build\viewport_test.obj /Fe:build\viewport-test.exe tests\viewport_test.cpp /link /Brepro
if errorlevel 1 exit /b %errorlevel%

cl /nologo /Brepro /std:c++20 /O2 /W4 /WX /EHsc /I src ^
  /Fo:build\movie_skip_gate_test.obj /Fe:build\movie-skip-gate-test.exe ^
  tests\movie_skip_gate_test.cpp /link /Brepro
if errorlevel 1 exit /b %errorlevel%

cl /nologo /Brepro /std:c++20 /O2 /W4 /WX /EHsc /MT /DWIN32 /LD /Fe:bin\aitd4-renderer-hook.dll ^
  /Fo:build\ ^
  src\hook_main.cpp src\runtime.cpp src\graphics_hook.cpp ^
  /link /Brepro /DEF:src\hook.def /INCREMENTAL:NO opengl32.lib gdi32.lib user32.lib bcrypt.lib
if errorlevel 1 exit /b %errorlevel%

cl /nologo /Brepro /std:c++20 /O2 /W4 /WX /EHsc /MT /DWIN32 /DAITD4_TEST_HARNESS /LD ^
  /Fo:build\ /Fe:build\aitd4-renderer-hook-test.dll ^
  src\hook_main.cpp src\runtime.cpp src\graphics_hook.cpp ^
  /link /Brepro /DEF:src\hook.def /INCREMENTAL:NO opengl32.lib gdi32.lib user32.lib bcrypt.lib
if errorlevel 1 exit /b %errorlevel%

cl /nologo /Brepro /std:c++20 /O2 /W4 /WX /EHsc /I src /Fe:build\aitd4-gl-harness.exe tests\gl_harness.cpp ^
  /Fo:build\gl_harness.obj ^
  /link /Brepro /INCREMENTAL:NO opengl32.lib gdi32.lib user32.lib
if errorlevel 1 exit /b %errorlevel%

cl /nologo /Brepro /std:c++20 /O2 /W4 /WX /EHsc /I src ^
  /Fo:build\dynamic_loader_harness.obj /Fe:build\aitd4-gl-harness-dynamic.exe ^
  tests\dynamic_loader_harness.cpp ^
  /link /Brepro /INCREMENTAL:NO user32.lib build\binkw32.lib
if errorlevel 1 exit /b %errorlevel%

cl /nologo /Brepro /std:c++20 /O2 /W4 /WX /EHsc /MT ^
  /Fo:build\dev_stack_injector.obj /Fe:build\aitd4-dev-stack-injector.exe ^
  tools\dev_stack_injector.cpp ^
  /link /Brepro /INCREMENTAL:NO
if errorlevel 1 exit /b %errorlevel%

copy /Y config\aitd4-overhaul.renderer.ini build\aitd4-overhaul.ini >nul
build\viewport-test.exe
if errorlevel 1 exit /b %errorlevel%
build\movie-skip-gate-test.exe
if errorlevel 1 exit /b %errorlevel%
build\aitd4-gl-harness.exe
if errorlevel 1 exit /b %errorlevel%
build\aitd4-gl-harness-dynamic.exe
if errorlevel 1 exit /b %errorlevel%
set AITD4_TEST_BINK_FIRST_OPEN_REJECT=1
build\aitd4-gl-harness-dynamic.exe
set fallback_result=%errorlevel%
set AITD4_TEST_BINK_FIRST_OPEN_REJECT=
if not "%fallback_result%"=="0" exit /b %fallback_result%
set AITD4_TEST_LOGICAL_WIDTH=1280
set AITD4_TEST_LOGICAL_HEIGHT=960
build\aitd4-gl-harness-dynamic.exe
set test_result=%errorlevel%
set AITD4_TEST_LOGICAL_WIDTH=
set AITD4_TEST_LOGICAL_HEIGHT=
if not "%test_result%"=="0" exit /b %test_result%
set AITD4_TEST_BINK_SCALE_REJECT=1
build\aitd4-gl-harness-dynamic.exe
set reject_result=%errorlevel%
set AITD4_TEST_BINK_SCALE_REJECT=
if not "%reject_result%"=="91" exit /b 92
echo rejecting Bink scale failed closed as expected
exit /b 0
