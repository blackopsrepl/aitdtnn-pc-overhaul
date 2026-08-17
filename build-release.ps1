[CmdletBinding()]
param([string]$Version)

$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($Version)) {
    $Version = (Get-Content -LiteralPath (Join-Path $root 'VERSION') -Raw).Trim()
}
if ($Version -notmatch '^\d+\.\d+\.\d+(\.\d+)?$') {
    throw "Version '$Version' is not valid for Windows version metadata."
}

& (Join-Path $root 'build.ps1')
if ($LASTEXITCODE -ne 0) { throw 'The native overhaul build failed.' }

$audioRoot = Join-Path $root 'audio-restoration'
$venv = Join-Path $root 'build\pyinstaller-venv'
$venvPython = Join-Path $venv 'Scripts\python.exe'
if (-not (Test-Path -LiteralPath $venvPython)) {
    & python -m venv $venv
    if ($LASTEXITCODE -ne 0) { throw 'Could not create the asset-builder environment.' }
}
& $venvPython -m pip install --disable-pip-version-check --quiet `
    -r (Join-Path $audioRoot 'tools\requirements-build.txt')
if ($LASTEXITCODE -ne 0) { throw 'Could not install the asset-builder dependencies.' }
& $venvPython -m PyInstaller --noconfirm --clean --onefile --name aitdtnn-assets `
    --distpath (Join-Path $audioRoot 'bin') `
    --workpath (Join-Path $root 'build\pyinstaller') `
    --specpath (Join-Path $root 'build') `
    (Join-Path $audioRoot 'tools\build_assets.py')
if ($LASTEXITCODE -ne 0) { throw 'Asset-builder packaging failed.' }

$isccCandidates = @(
    $env:ISCC,
    'C:\Program Files (x86)\Inno Setup 6\ISCC.exe',
    'C:\Program Files\Inno Setup 6\ISCC.exe',
    (Join-Path $env:LOCALAPPDATA 'Programs\Inno Setup 6\ISCC.exe')
) | Where-Object { $_ -and (Test-Path -LiteralPath $_) }
$iscc = $isccCandidates | Select-Object -First 1
if (-not $iscc) { throw 'Inno Setup 6 was not found.' }

$releaseRoot = Join-Path $root 'build\release'
if (Test-Path -LiteralPath $releaseRoot) {
    Remove-Item -LiteralPath $releaseRoot -Recurse -Force
}
New-Item -ItemType Directory -Path $releaseRoot -Force | Out-Null
& $iscc "/DMyAppVersion=$Version" (Join-Path $root 'installer\AITDTNN-PC-Overhaul.iss')
if ($LASTEXITCODE -ne 0) { throw 'The combined Windows Setup build failed.' }

$setup = Join-Path $releaseRoot "AITDTNN-PC-Overhaul-Setup-$Version.exe"
if (-not (Test-Path -LiteralPath $setup -PathType Leaf)) {
    throw 'The expected combined Setup executable was not produced.'
}
$hash = (Get-FileHash -LiteralPath $setup -Algorithm SHA256).Hash.ToLowerInvariant()
"$hash  $([IO.Path]::GetFileName($setup))" |
    Set-Content -LiteralPath (Join-Path $releaseRoot 'SHA256SUMS.txt') -Encoding ascii
Write-Host "Combined installer: $setup"
