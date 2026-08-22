[CmdletBinding()]
# Transaction dispatcher for install, rollback and uninstall. Destructive
# primitives live in Manage-Overhaul.Core.ps1; this file shows state transitions
# in chronological order after validating the caller's requested mode.
param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('BeginInstall', 'FinalizeInstall', 'Rollback', 'Uninstall')]
    [string]$Mode,

    [Parameter(Mandatory = $true)]
    [string]$GamePath,

    [Parameter(Mandatory = $true)]
    [string]$AppPath,

    [string]$BackupPath
)

$ErrorActionPreference = 'Stop'
$supportedExeHashes = @(
    '5668118E0E19D569986500A1C805A85397C8681E7B672B49A68645462ECCC672', # English 15-slot/no-CD
    '320908AF4CE5C724B60A7EEA6A5AADE737D51D65AEE8506744FCE6E6DD0143E0'  # English retail CD
)
$gameRoot = [IO.Path]::GetFullPath($GamePath.Trim().TrimEnd('\'))
$appRoot = [IO.Path]::GetFullPath($AppPath.Trim().TrimEnd('\'))
$audioRoot = Join-Path $gameRoot 'audio-restoration'
$rendererRoot = Join-Path $gameRoot 'renderer'
$rumbleRoot = Join-Path $gameRoot 'rumble'
$loaderPath = Join-Path $gameRoot 'version.dll'
$controllerFileNames = @('dinput8.dll', 'winmm.dll', 'Xidi.32.dll', 'Xidi.ini', 'keys.bin')
$loaderLog = Join-Path $gameRoot 'aitdtnn-overhaul-loader.log'
$installerErrorLog = Join-Path $gameRoot 'aitdtnn-overhaul-installer-error.log'

trap {
    ($_ | Out-String) | Add-Content -LiteralPath $installerErrorLog -Encoding UTF8
    exit 1
}


# Load the auditable filesystem/ownership operations before dispatching a mode.
. (Join-Path $PSScriptRoot 'Manage-Overhaul.Core.ps1')

if (-not (Test-Path -LiteralPath $gameRoot -PathType Container)) {
    throw "Game directory does not exist: $gameRoot"
}
Assert-WithinGame $appRoot | Out-Null

switch ($Mode) {
    'BeginInstall' {
        if (Get-Process -Name alone4 -ErrorAction SilentlyContinue) {
            throw 'Close Alone in the Dark before installing the overhaul.'
        }
        $exe = Join-Path $gameRoot 'alone4.exe'
        if (-not (Test-Path -LiteralPath $exe -PathType Leaf)) {
            throw "alone4.exe was not found in $gameRoot"
        }
        $actual = Get-Sha256 $exe
        if ($actual -notin $supportedExeHashes) {
            throw "Unsupported alone4.exe SHA-256: $actual"
        }
        if ((Test-Path -LiteralPath (Join-Path $appRoot 'ownership.json') -PathType Leaf) -or
            (Test-Path -LiteralPath (Join-Path $appRoot 'backup\previous'))) {
            throw 'An integrated overhaul is already installed. Uninstall it before installing this version; in-place upgrades are refused to preserve rollback ownership.'
        }
        if ((Test-Path -LiteralPath $appRoot -PathType Container) -and
            (Get-ChildItem -LiteralPath $appRoot -Force | Select-Object -First 1)) {
            throw "The overhaul metadata directory is not empty; refusing to overwrite it: $appRoot"
        }
        if ([string]::IsNullOrWhiteSpace($BackupPath)) {
            throw 'The installer did not provide a transaction backup path.'
        }
        $backup = Assert-WithinGame $BackupPath
        if (-not ([IO.Path]::GetFileName($backup)).StartsWith('.aitdtnn-overhaul-install-backup-')) {
            throw "Refusing an unexpected transaction backup path: $backup"
        }
        if (Test-Path -LiteralPath $backup) {
            throw "Transaction backup already exists: $backup"
        }
        New-Item -ItemType Directory -Path $backup | Out-Null
        Move-IfPresent $audioRoot (Join-Path $backup 'audio-restoration')
        Move-IfPresent $rendererRoot (Join-Path $backup 'renderer')
        Move-IfPresent $rumbleRoot (Join-Path $backup 'rumble')
        Move-IfPresent $loaderPath (Join-Path $backup 'version.dll')
        foreach ($name in $controllerFileNames) {
            Move-IfPresent (Join-Path $gameRoot $name) (Join-Path $backup $name)
        }
        [ordered]@{
            format = 1
            game_executable_sha256 = $actual
            begun_utc = [DateTime]::UtcNow.ToString('o')
        } | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $backup 'transaction.json') -Encoding UTF8
        break
    }

    'FinalizeInstall' {
        if ([string]::IsNullOrWhiteSpace($BackupPath)) {
            throw 'The installer did not provide the transaction backup path.'
        }
        $backup = Assert-WithinGame $BackupPath
        foreach ($required in @(
            $loaderPath,
            (Join-Path $audioRoot 'aitd4-audio-hook.dll'),
            (Join-Path $audioRoot 'runtime-assets\asset-manifest.json'),
            (Join-Path $rendererRoot 'aitd4-renderer-hook.dll'),
            (Join-Path $rendererRoot 'aitd4-overhaul.ini'),
            (Join-Path $rendererRoot 'shaders\compositor.vert'),
            (Join-Path $rendererRoot 'shaders\compositor.frag'),
            (Join-Path $rendererRoot 'shaders\crt_signal.frag'),
            (Join-Path $rendererRoot 'shaders\crt_response.frag'),
            (Join-Path $rendererRoot 'shaders\crt_blur.frag'),
            (Join-Path $rendererRoot 'shaders\crt_present.frag'),
            (Join-Path $rumbleRoot 'aitd4-rumble-hook.dll'),
            (Join-Path $rumbleRoot 'aitd4-rumble.ini'),
            (Join-Path $gameRoot 'dinput8.dll'),
            (Join-Path $gameRoot 'winmm.dll'),
            (Join-Path $gameRoot 'Xidi.32.dll'),
            (Join-Path $gameRoot 'Xidi.ini'),
            (Join-Path $gameRoot 'keys.bin')
        )) {
            if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
                throw "Required installed payload is missing: $required"
            }
        }
        $previous = Join-Path $appRoot 'backup\previous'
        if (Test-Path -LiteralPath $previous) {
            throw "Previous-install backup already exists: $previous"
        }
        New-Item -ItemType Directory -Path $appRoot -Force | Out-Null
        $ownedFiles = @(Get-OwnedFiles)
        $installedExeHash = Get-Sha256 (Join-Path $gameRoot 'alone4.exe')
        if ($installedExeHash -notin $supportedExeHashes) {
            throw "Unsupported alone4.exe SHA-256 during install finalization: $installedExeHash"
        }
        [ordered]@{
            format = 1
            installed_utc = [DateTime]::UtcNow.ToString('o')
            game_executable_sha256 = $installedExeHash
            files = $ownedFiles
        } | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $appRoot 'ownership.json') -Encoding UTF8
        New-Item -ItemType Directory -Path (Split-Path -Parent $previous) -Force | Out-Null
        Move-Item -LiteralPath $backup -Destination $previous
        break
    }

    'Rollback' {
        $previous = $null
        if (-not [string]::IsNullOrWhiteSpace($BackupPath) -and (Test-Path -LiteralPath $BackupPath)) {
            $previous = Assert-WithinGame $BackupPath
        } elseif (Test-Path -LiteralPath (Join-Path $appRoot 'backup\previous')) {
            $previous = Join-Path $appRoot 'backup\previous'
        }
        $failed = Preserve-CurrentInstall
        if ($previous) { Restore-Previous $previous }
        Write-Output "Incomplete install preserved at: $failed"
        break
    }

    'Uninstall' {
        if (Get-Process -Name alone4 -ErrorAction SilentlyContinue) {
            throw 'Close Alone in the Dark before uninstalling the overhaul.'
        }
        $manifestPath = Join-Path $appRoot 'ownership.json'
        if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
            throw 'Ownership manifest is missing; refusing an unsafe uninstall.'
        }
        $manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
        $unchanged = Test-InstalledTree @($manifest.files)
        if ($unchanged) {
            foreach ($path in $audioRoot, $rendererRoot, $rumbleRoot) {
                Assert-WithinGame $path | Out-Null
                if (Test-Path -LiteralPath $path) {
                    Remove-Item -LiteralPath $path -Recurse -Force
                }
            }
            if (Test-Path -LiteralPath $loaderPath -PathType Leaf) {
                Remove-Item -LiteralPath $loaderPath -Force
            }
            foreach ($name in $controllerFileNames) {
                $path = Join-Path $gameRoot $name
                if (Test-Path -LiteralPath $path -PathType Leaf) {
                    Remove-Item -LiteralPath $path -Force
                }
            }
            Remove-OwnedAppFiles @($manifest.files)
        } else {
            $preserved = Preserve-CurrentInstall
            Set-Content -LiteralPath (Join-Path $preserved 'README.txt') -Encoding UTF8 -Value @(
                'This folder contains files that differed from the installed overhaul manifest.',
                'They were preserved so uninstall could restore the previous game stack without data loss.'
            )
        }
        $previous = Join-Path $appRoot 'backup\previous'
        if (Test-Path -LiteralPath $previous) {
            Restore-Previous $previous
            $transaction = Join-Path $previous 'transaction.json'
            if (Test-Path -LiteralPath $transaction -PathType Leaf) {
                Remove-Item -LiteralPath $transaction -Force
            }
            if (-not (Get-ChildItem -LiteralPath $previous -Force | Select-Object -First 1)) {
                Remove-Item -LiteralPath $previous -Force
            }
            $backupRoot = Join-Path $appRoot 'backup'
            if ((Test-Path -LiteralPath $backupRoot -PathType Container) -and
                -not (Get-ChildItem -LiteralPath $backupRoot -Force | Select-Object -First 1)) {
                Remove-Item -LiteralPath $backupRoot -Force
            }
        }
        if (Test-Path -LiteralPath $loaderLog -PathType Leaf) {
            Remove-Item -LiteralPath $loaderLog -Force
        }
        break
    }
}
