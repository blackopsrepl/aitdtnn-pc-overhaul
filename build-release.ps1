[CmdletBinding()]
param(
    [string]$Version,
    [switch]$Development
)

$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($Version)) {
    $Version = (Get-Content -LiteralPath (Join-Path $root 'VERSION') -Raw).Trim()
}
if ($Version -notmatch '^\d+\.\d+\.\d+(\.\d+)?$') {
    throw "Version '$Version' is not valid for Windows version metadata."
}
if (-not $Development) {
    $git = Get-Command git.exe -ErrorAction Stop
    $status = & $git.Source -C $root status --porcelain=v1
    if ($LASTEXITCODE -ne 0) { throw 'Could not inspect the source repository.' }
    if ($status) {
        throw 'Publication builds require a clean committed source tree. Use -Development only for a non-publishable test installer.'
    }
}

$payloadManifestPath = Join-Path $root 'payload\working-payload.json'
if (-not (Test-Path -LiteralPath $payloadManifestPath -PathType Leaf)) {
    throw 'The validated working-runtime payload manifest is missing.'
}
$payloadManifest = Get-Content -LiteralPath $payloadManifestPath -Raw | ConvertFrom-Json
foreach ($record in @($payloadManifest.files)) {
    $payloadPath = Join-Path (Join-Path $root 'payload') ([string]$record.path).Replace('/', '\')
    if (-not (Test-Path -LiteralPath $payloadPath -PathType Leaf)) {
        throw "Working-runtime payload file is missing: $payloadPath"
    }
    $actualPayloadHash = (Get-FileHash -LiteralPath $payloadPath -Algorithm SHA256).Hash
    if ($actualPayloadHash -ne [string]$record.sha256) {
        throw "Working-runtime payload hash mismatch: $payloadPath"
    }
}
Write-Host "Validated $(@($payloadManifest.files).Count) exact working-runtime payload files."

$audioRoot = Join-Path $root 'audio-restoration'
$venv = Join-Path $root 'build\pyinstaller-venv'
$venvPython = Join-Path $venv 'Scripts\python.exe'
if (-not (Test-Path -LiteralPath $venvPython)) {
    $pythonCommand = Get-Command python.exe -ErrorAction SilentlyContinue
    $pythonPrefix = @()
    if (-not $pythonCommand) {
        $pythonCommand = Get-Command py.exe -ErrorAction SilentlyContinue
        $pythonPrefix = @('-3')
    }
    if (-not $pythonCommand) {
        throw 'CPython 3.10 was not found.'
    }
    $pythonVersion = & $pythonCommand.Source @pythonPrefix -c `
        'import sys; print(".".join(map(str, sys.version_info[:3])))'
    $parsedPythonVersion = [version]$pythonVersion
    if ($parsedPythonVersion -ne [version]'3.10.2') {
        throw "CPython 3.10.2 is required for the pinned release environment; found $pythonVersion."
    }
    & $pythonCommand.Source @pythonPrefix -m venv $venv
    if ($LASTEXITCODE -ne 0) { throw 'Could not create the asset-builder environment.' }
}
$venvVersion = & $venvPython -c 'import sys; print(".".join(map(str, sys.version_info[:3])))'
if ($LASTEXITCODE -ne 0) { throw 'Could not query the asset-builder Python environment.' }
$parsedVenvVersion = [version]$venvVersion
if ($parsedVenvVersion -ne [version]'3.10.2') {
    throw "The release environment must use CPython 3.10.2; found $venvVersion."
}
$venvBits = & $venvPython -c 'import struct; print(struct.calcsize("P") * 8)'
if ($LASTEXITCODE -ne 0 -or $venvBits -ne '64') {
    throw "The release environment must use 64-bit CPython; found $venvBits-bit."
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
$releaseFiles = @($setup)
if (-not $Development) {
    $sourceArchive = Join-Path $releaseRoot "AITDTNN-PC-Overhaul-Source-$Version.zip"
    & git.exe -C $root archive --format=zip `
        "--prefix=AITDTNN-PC-Overhaul-$Version/" `
        "--output=$sourceArchive" HEAD
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $sourceArchive -PathType Leaf)) {
        throw 'Could not create the exact corresponding-source archive.'
    }
    $releaseFiles += $sourceArchive
} else {
    Write-Warning 'Development installer built from a dirty/uncommitted tree; do not publish it.'
}
$sums = foreach ($file in $releaseFiles) {
    $hash = (Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash.ToLowerInvariant()
    "$hash  $([IO.Path]::GetFileName($file))"
}
$sums | Set-Content -LiteralPath (Join-Path $releaseRoot 'SHA256SUMS.txt') -Encoding ascii
Write-Host "Combined installer: $setup"
