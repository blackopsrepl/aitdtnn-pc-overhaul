[CmdletBinding()]
param(
    [switch]$SkipModules
)

$ErrorActionPreference = 'Stop'
$repoRoot = $PSScriptRoot
$audioRoot = Join-Path $repoRoot 'audio-restoration'
$rendererRoot = Join-Path $repoRoot 'renderer'
$rumbleRoot = Join-Path $repoRoot 'rumble'
$loaderRoot = Join-Path $repoRoot 'shared\loader'
$sourceRoot = Join-Path $loaderRoot 'src'
$buildRoot = Join-Path $repoRoot 'build\shared-loader'

if (-not $SkipModules) {
    & (Join-Path $audioRoot 'build.ps1')
    if ($LASTEXITCODE -ne 0) {
        throw "The audio-restoration module build failed with exit code $LASTEXITCODE."
    }
    & (Join-Path $rendererRoot 'build.ps1')
    if ($LASTEXITCODE -ne 0) {
        throw "The renderer module build failed with exit code $LASTEXITCODE."
    }
    & (Join-Path $rumbleRoot 'build.ps1')
    if ($LASTEXITCODE -ne 0) {
        throw "The rumble module build failed with exit code $LASTEXITCODE."
    }
}

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere)) {
    throw 'Visual Studio Installer\vswhere.exe was not found.'
}
$visualStudio = & $vswhere -latest -products * `
    -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
    -property installationPath
if (-not $visualStudio) {
    throw 'A Visual Studio installation with the x86 C++ toolchain is required.'
}
$vsDevCmd = Join-Path $visualStudio 'Common7\Tools\VsDevCmd.bat'
if (-not (Test-Path -LiteralPath $vsDevCmd)) {
    throw "VsDevCmd.bat was not found at $vsDevCmd"
}

New-Item -ItemType Directory -Path $buildRoot -Force | Out-Null

$versionSource = Join-Path $sourceRoot 'version_proxy.cpp'
$shaSource = Join-Path $sourceRoot 'sha256.cpp'
$testSource = Join-Path $sourceRoot 'sha256_test.cpp'
$definition = Join-Path $loaderRoot 'version.def'
$proxyObject = Join-Path $buildRoot 'version_proxy.obj'
$shaObject = Join-Path $buildRoot 'sha256.obj'
$testObject = Join-Path $buildRoot 'sha256_test.obj'
$proxyDll = Join-Path $buildRoot 'version.dll'
$testExe = Join-Path $buildRoot 'sha256-test.exe'
$commands = Join-Path $buildRoot 'compile.cmd'

$script = @"
@echo off
call "$vsDevCmd" -no_logo -arch=x86 -host_arch=x64
if errorlevel 1 exit /b %errorlevel%
cl.exe /nologo /c /Brepro /std:c++17 /O2 /W4 /WX /MT /EHsc- /GR- /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN /Fo"$proxyObject" "$versionSource"
if errorlevel 1 exit /b %errorlevel%
cl.exe /nologo /c /Brepro /std:c++17 /O2 /W4 /WX /MT /EHsc- /GR- /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN /Fo"$shaObject" "$shaSource"
if errorlevel 1 exit /b %errorlevel%
link.exe /nologo /Brepro /DLL /MACHINE:X86 /DYNAMICBASE /NXCOMPAT /OPT:REF /OPT:ICF /DEF:"$definition" /OUT:"$proxyDll" "$proxyObject" "$shaObject" kernel32.lib user32.lib
if errorlevel 1 exit /b %errorlevel%
cl.exe /nologo /c /Brepro /std:c++17 /O2 /W4 /WX /MT /EHsc- /GR- /Fo"$testObject" "$testSource"
if errorlevel 1 exit /b %errorlevel%
link.exe /nologo /Brepro /MACHINE:X86 /SUBSYSTEM:CONSOLE /OUT:"$testExe" "$testObject" "$shaObject" kernel32.lib
if errorlevel 1 exit /b %errorlevel%
"$testExe"
exit /b %errorlevel%
"@
[System.IO.File]::WriteAllText($commands, $script, [System.Text.Encoding]::ASCII)

& cmd.exe /d /c $commands
if ($LASTEXITCODE -ne 0) {
    throw "The x86 loader build failed with exit code $LASTEXITCODE."
}

& (Join-Path $loaderRoot 'verify.ps1') -ProxyPath $proxyDll
if ($LASTEXITCODE -ne 0) {
    throw "Static loader verification failed with exit code $LASTEXITCODE."
}

foreach ($componentRoot in $rendererRoot, $rumbleRoot) {
    $manifestPath = Join-Path $componentRoot 'bin\component.json'
    $manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
    foreach ($entry in $manifest.payload) {
        $source = Join-Path $componentRoot ([string]$entry.source).Replace('/', '\')
        $actual = (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($actual -ne [string]$entry.sha256) {
            throw "Component manifest hash mismatch: $source"
        }
    }
}

Write-Host "Built: $proxyDll"
