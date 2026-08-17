[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$SetupPath,

    [Parameter(Mandatory = $true)]
    [string]$GameExe,

    [Parameter(Mandatory = $true)]
    [string]$DreamcastImage,

    [string]$ScratchRoot = "$env:TEMP\aitdtnn-installer-lifecycle-$PID",

    [switch]$Keep
)

$ErrorActionPreference = 'Stop'
$supportedHash = '5668118E0E19D569986500A1C805A85397C8681E7B672B49A68645462ECCC672'
$setup = (Resolve-Path -LiteralPath $SetupPath).Path
$sourceExe = (Resolve-Path -LiteralPath $GameExe).Path
$image = (Resolve-Path -LiteralPath $DreamcastImage).Path
$testRoot = [IO.Path]::GetFullPath($ScratchRoot.Trim().TrimEnd('\'))
$payloadManifestPath = Join-Path $PSScriptRoot '..\..\payload\working-payload.json'
$payloadManifest = Get-Content -LiteralPath $payloadManifestPath -Raw | ConvertFrom-Json
$controllerNames = @('dinput8.dll', 'winmm.dll', 'Xidi.32.dll', 'Xidi.ini', 'keys.bin')

if ([IO.Path]::GetFileName($testRoot) -notlike 'aitdtnn-installer-lifecycle-*') {
    throw "ScratchRoot must end in an aitdtnn-installer-lifecycle-* directory: $testRoot"
}
if ((Get-FileHash -LiteralPath $sourceExe -Algorithm SHA256).Hash -ne $supportedHash) {
    throw 'GameExe is not the exact supported executable.'
}
if (Get-Process -Name alone4 -ErrorAction SilentlyContinue) {
    throw 'Close alone4.exe before running the installer lifecycle test.'
}

function Assert-True([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
}

function Clear-TestRoot {
    if (-not [IO.Directory]::Exists($testRoot)) { return }
    Get-ChildItem -LiteralPath $testRoot -Force -Recurse -ErrorAction SilentlyContinue |
        ForEach-Object { try { $_.IsReadOnly = $false } catch {} }
    [IO.Directory]::Delete($testRoot, $true)
}

function New-Fixture {
    Clear-TestRoot
    New-Item -ItemType Directory -Path $testRoot | Out-Null
    Copy-Item -LiteralPath $sourceExe -Destination (Join-Path $testRoot 'alone4.exe')
    foreach ($name in 'audio-restoration', 'renderer', 'rumble') {
        New-Item -ItemType Directory -Path (Join-Path $testRoot $name) | Out-Null
        Set-Content -LiteralPath (Join-Path $testRoot "$name\prior.txt") `
            -Value "prior-$name" -Encoding ascii
    }
    Set-Content -LiteralPath (Join-Path $testRoot 'version.dll') `
        -Value 'prior-version' -Encoding ascii
    foreach ($name in $controllerNames) {
        Set-Content -LiteralPath (Join-Path $testRoot $name) `
            -Value "prior-$name" -Encoding ascii
    }
}

function Invoke-Setup([string]$LogName) {
    $log = Join-Path $testRoot $LogName
    $arguments = '/VERYSILENT /SUPPRESSMSGBOXES /NORESTART ' +
        '/GAMEPATH="' + $testRoot + '" ' +
        '/DREAMCASTIMAGE="' + $image + '" ' +
        '/LOG="' + $log + '"'
    return Start-Process -FilePath $setup -ArgumentList $arguments -Wait `
        -PassThru -WindowStyle Hidden
}

function Invoke-Uninstall([string]$LogName) {
    $uninstaller = Get-ChildItem -LiteralPath (Join-Path $testRoot 'aitdtnn-overhaul') `
        -Filter 'unins*.exe' -File | Sort-Object Name | Select-Object -First 1
    if (-not $uninstaller) { throw 'The installed uninstaller was not found.' }
    $log = Join-Path $testRoot $LogName
    $arguments = '/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /LOG="' + $log + '"'
    return Start-Process -FilePath $uninstaller.FullName -ArgumentList $arguments `
        -Wait -PassThru -WindowStyle Hidden
}

function Assert-PreviousStack {
    Assert-True (((Get-Content -LiteralPath (Join-Path $testRoot 'version.dll') -Raw).Trim()) `
        -eq 'prior-version') 'Previous version.dll was not restored.'
    foreach ($name in 'audio-restoration', 'renderer', 'rumble') {
        $value = (Get-Content -LiteralPath (Join-Path $testRoot "$name\prior.txt") -Raw).Trim()
        Assert-True ($value -eq "prior-$name") "Previous $name was not restored."
    }
    foreach ($name in $controllerNames) {
        $value = (Get-Content -LiteralPath (Join-Path $testRoot $name) -Raw).Trim()
        Assert-True ($value -eq "prior-$name") "Previous $name was not restored."
    }
    Assert-True (((Get-FileHash -LiteralPath (Join-Path $testRoot 'alone4.exe') `
        -Algorithm SHA256).Hash) -eq $supportedHash) 'alone4.exe changed.'
}

function Assert-WorkingPayload {
    foreach ($record in @($payloadManifest.files)) {
        $relative = ([string]$record.path) -replace '^game/', ''
        $path = Join-Path $testRoot $relative.Replace('/', '\')
        Assert-True (Test-Path -LiteralPath $path -PathType Leaf) `
            "Working payload file was not installed: $relative"
        $actual = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash
        Assert-True ($actual -eq [string]$record.sha256) `
            "Working payload hash mismatch: $relative"
    }
}

try {
    New-Fixture
    $install = Invoke-Setup 'fresh-install.log'
    Assert-True ($install.ExitCode -eq 0) "Fresh install failed: $($install.ExitCode)"
    $ownership = Join-Path $testRoot 'aitdtnn-overhaul\ownership.json'
    Assert-True (Test-Path -LiteralPath $ownership -PathType Leaf) `
        'Ownership manifest was not installed.'
    Assert-WorkingPayload
    $ownershipHash = (Get-FileHash -LiteralPath $ownership -Algorithm SHA256).Hash
    $rendererHash = (Get-FileHash -LiteralPath `
        (Join-Path $testRoot 'renderer\aitd4-renderer-hook.dll') -Algorithm SHA256).Hash

    $upgrade = Invoke-Setup 'upgrade-refusal.log'
    Assert-True ($upgrade.ExitCode -ne 0) 'In-place upgrade unexpectedly succeeded.'
    Assert-True (((Get-FileHash -LiteralPath $ownership -Algorithm SHA256).Hash) `
        -eq $ownershipHash) 'Upgrade refusal changed ownership metadata.'
    Assert-True (((Get-FileHash -LiteralPath `
        (Join-Path $testRoot 'renderer\aitd4-renderer-hook.dll') -Algorithm SHA256).Hash) `
        -eq $rendererHash) 'Upgrade refusal changed the payload.'

    $uninstall = Invoke-Uninstall 'normal-uninstall.log'
    Assert-True ($uninstall.ExitCode -eq 0) "Normal uninstall failed: $($uninstall.ExitCode)"
    Assert-PreviousStack
    Assert-True (-not (Test-Path -LiteralPath (Join-Path $testRoot 'aitdtnn-overhaul'))) `
        'Unchanged installer metadata was not removed.'

    $reinstall = Invoke-Setup 'modified-install.log'
    Assert-True ($reinstall.ExitCode -eq 0) "Modified-tree install failed: $($reinstall.ExitCode)"
    Add-Content -LiteralPath (Join-Path $testRoot 'renderer\aitd4-renderer-hook.dll') `
        -Value 'user-modified'
    Set-Content -LiteralPath (Join-Path $testRoot 'renderer\user-added.txt') `
        -Value 'user-added' -Encoding ascii
    Add-Content -LiteralPath (Join-Path $testRoot 'aitdtnn-overhaul\README.md') `
        -Value 'user-modified-doc'
    Set-Content -LiteralPath (Join-Path $testRoot 'aitdtnn-overhaul\user-note.txt') `
        -Value 'user-note' -Encoding ascii

    $modifiedUninstall = Invoke-Uninstall 'modified-uninstall.log'
    Assert-True ($modifiedUninstall.ExitCode -eq 0) `
        "Modified-tree uninstall failed: $($modifiedUninstall.ExitCode)"
    Assert-PreviousStack
    $preserved = Get-ChildItem -LiteralPath $testRoot -Directory `
        -Filter 'aitdtnn-overhaul-preserved-*' | Select-Object -First 1
    Assert-True ($null -ne $preserved) 'Modified installation was not preserved.'
    Assert-True (Test-Path -LiteralPath `
        (Join-Path $preserved.FullName 'renderer\user-added.txt')) `
        'Unknown game file was not preserved.'
    Assert-True (Test-Path -LiteralPath `
        (Join-Path $preserved.FullName 'aitdtnn-overhaul\user-note.txt')) `
        'Unknown installer metadata was not preserved.'

    Write-Host 'Installer lifecycle tests passed.'
} finally {
    if (-not $Keep) { Clear-TestRoot }
}
