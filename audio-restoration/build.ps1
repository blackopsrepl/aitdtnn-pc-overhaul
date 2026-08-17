[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$vcvarsCandidates = @(
    'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars32.bat',
    'C:\Program Files\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars32.bat',
    'C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars32.bat',
    'C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars32.bat'
)
$vcvars = $vcvarsCandidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
if (-not $vcvars) { throw 'Visual Studio 2022 with the Desktop development with C++ workload was not found.' }

New-Item -ItemType Directory -Force -Path (Join-Path $root 'build\core'), (Join-Path $root 'build\native'), (Join-Path $root 'bin') | Out-Null
Remove-Item -LiteralPath (Join-Path $root 'bin\aitd4-injector.exe') -Force -ErrorAction SilentlyContinue
$commands = @(
    ('call "{0}" >nul' -f $vcvars),
    ('cd /d "{0}"' -f $root),
    'cl /nologo /c /Brepro /O2 /MT /TC /DWIN32 /DNDEBUG /D_LIB /DEMU_COMPILE /DEMU_LITTLE_ENDIAN /Fo:build\core\ third_party\highly_theoretical\Core\arm.c third_party\highly_theoretical\Core\dcsound.c third_party\highly_theoretical\Core\yam.c',
    'lib /nologo /out:build\SegaCore.lib build\core\arm.obj build\core\dcsound.obj build\core\yam.obj',
    'cl /nologo /Brepro /std:c++20 /O2 /EHsc /MT /Isrc /Fo:build\native\ /Fe:build\music-runtime-tests.exe tests\music_runtime_tests.cpp /link /Brepro',
    'build\music-runtime-tests.exe',
    'cl /nologo /Brepro /std:c++20 /O2 /EHsc /MT /DWIN32 /Ithird_party\highly_theoretical\Core /Fo:build\native\ /LD /Fe:bin\aitd4-audio-hook.dll src\audio_hook.cpp src\audio_renderer.cpp build\SegaCore.lib /link /Brepro /INCREMENTAL:NO /DEF:src\audio_hook.def /IMPLIB:build\native\aitd4-audio-hook.lib winmm.lib',
    'dumpbin /nologo /exports bin\aitd4-audio-hook.dll > build\audio-hook-exports.txt'
) -join ' && '

& cmd.exe /d /c $commands
if ($LASTEXITCODE -ne 0) { throw "Native build failed with exit code $LASTEXITCODE." }
$exports = Get-Content -LiteralPath (Join-Path $root 'build\audio-hook-exports.txt')
$namedExport = $exports | Where-Object { $_ -match '\sAITD4_AudioInitialize$' }
if ($namedExport.Count -ne 1 -or $exports -match '_AITD4_AudioInitialize@\d+$') {
    throw 'The audio hook must export exactly one undecorated AITD4_AudioInitialize entry.'
}
Write-Host 'Native binaries written to bin\.'
