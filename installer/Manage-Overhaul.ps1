[CmdletBinding()]
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
$supportedExeHash = '5668118E0E19D569986500A1C805A85397C8681E7B672B49A68645462ECCC672'
$gameRoot = [IO.Path]::GetFullPath($GamePath.Trim().TrimEnd('\'))
$appRoot = [IO.Path]::GetFullPath($AppPath.Trim().TrimEnd('\'))
$audioRoot = Join-Path $gameRoot 'audio-restoration'
$rendererRoot = Join-Path $gameRoot 'renderer'
$rumbleRoot = Join-Path $gameRoot 'rumble'
$loaderPath = Join-Path $gameRoot 'version.dll'
$loaderLog = Join-Path $gameRoot 'aitdtnn-overhaul-loader.log'
$installerErrorLog = Join-Path $gameRoot 'aitdtnn-overhaul-installer-error.log'

trap {
    ($_ | Out-String) | Add-Content -LiteralPath $installerErrorLog -Encoding UTF8
    exit 1
}

function Assert-WithinGame([string]$Path, [switch]$AllowGameRoot) {
    $full = [IO.Path]::GetFullPath($Path.Trim().TrimEnd('\'))
    if ($AllowGameRoot -and $full.Equals($gameRoot, [StringComparison]::OrdinalIgnoreCase)) {
        return $full
    }
    $prefix = $gameRoot + [IO.Path]::DirectorySeparatorChar
    if (-not $full.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing a path outside the selected game directory: $full"
    }
    return $full
}

function Move-IfPresent([string]$Source, [string]$Destination) {
    $Source = Assert-WithinGame $Source
    $Destination = Assert-WithinGame $Destination
    if (-not (Test-Path -LiteralPath $Source)) { return }
    $parent = Split-Path -Parent $Destination
    if ($parent) { New-Item -ItemType Directory -Path $parent -Force | Out-Null }
    if (Test-Path -LiteralPath $Destination) {
        throw "Backup destination already exists: $Destination"
    }
    Move-Item -LiteralPath $Source -Destination $Destination
}

function Restore-Previous([string]$PreviousRoot) {
    $PreviousRoot = Assert-WithinGame $PreviousRoot
    foreach ($name in 'audio-restoration', 'renderer', 'rumble', 'version.dll') {
        $source = Join-Path $PreviousRoot $name
        $destination = Join-Path $gameRoot $name
        if (-not (Test-Path -LiteralPath $source)) { continue }
        if (Test-Path -LiteralPath $destination) {
            throw "Refusing to overwrite a later file while restoring: $destination"
        }
        Move-Item -LiteralPath $source -Destination $destination
    }
}

function Get-Sha256([string]$Path) {
    $stream = [IO.File]::Open($Path, [IO.FileMode]::Open, [IO.FileAccess]::Read,
        [IO.FileShare]::ReadWrite -bor [IO.FileShare]::Delete)
    $sha = [Security.Cryptography.SHA256]::Create()
    try {
        $digest = $sha.ComputeHash($stream)
        return ([BitConverter]::ToString($digest)).Replace('-', '')
    } finally {
        $sha.Dispose()
        $stream.Dispose()
    }
}

function Get-OwnedFiles {
    $files = New-Object 'System.Collections.Generic.List[object]'
    if (Test-Path -LiteralPath $loaderPath -PathType Leaf) {
        $files.Add([ordered]@{
            path = 'version.dll'
            sha256 = Get-Sha256 $loaderPath
        })
    }
    foreach ($root in $audioRoot, $rendererRoot, $rumbleRoot) {
        if (-not (Test-Path -LiteralPath $root -PathType Container)) { continue }
        foreach ($file in Get-ChildItem -LiteralPath $root -Recurse -File) {
            $relative = $file.FullName.Substring($gameRoot.Length + 1).Replace('\', '/')
            $files.Add([ordered]@{
                path = $relative
                sha256 = Get-Sha256 $file.FullName
            })
        }
    }
    return $files
}

function Test-InstalledTree([object[]]$OwnedFiles) {
    $owned = @{}
    foreach ($record in $OwnedFiles) {
        $relative = [string]$record.path
        $owned[$relative.ToLowerInvariant()] = [string]$record.sha256
        $path = Join-Path $gameRoot $relative.Replace('/', '\')
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { return $false }
        if ((Get-Sha256 $path) -ne [string]$record.sha256) {
            return $false
        }
    }

    $allowedMutable = @(
        'audio-restoration/aitd4-audio-hook.log',
        'renderer/aitd4-renderer.log',
        'rumble/aitd4-rumble-hook.log'
    )
    foreach ($root in $audioRoot, $rendererRoot, $rumbleRoot) {
        if (-not (Test-Path -LiteralPath $root -PathType Container)) { continue }
        foreach ($file in Get-ChildItem -LiteralPath $root -Recurse -File) {
            $relative = $file.FullName.Substring($gameRoot.Length + 1).Replace('\', '/')
            $key = $relative.ToLowerInvariant()
            if (-not $owned.ContainsKey($key) -and $key -notin $allowedMutable) {
                return $false
            }
        }
    }
    return $true
}

function Preserve-CurrentInstall {
    $stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
    $preserved = Join-Path $gameRoot "aitdtnn-overhaul-preserved-$stamp"
    if (Test-Path -LiteralPath $preserved) {
        throw "Preservation target already exists: $preserved"
    }
    New-Item -ItemType Directory -Path $preserved | Out-Null
    Move-IfPresent $audioRoot (Join-Path $preserved 'audio-restoration')
    Move-IfPresent $rendererRoot (Join-Path $preserved 'renderer')
    Move-IfPresent $rumbleRoot (Join-Path $preserved 'rumble')
    Move-IfPresent $loaderPath (Join-Path $preserved 'version.dll')
    return $preserved
}

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
        if ($actual -ne $supportedExeHash) {
            throw "Unsupported alone4.exe SHA-256: $actual"
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
            (Join-Path $rumbleRoot 'aitd4-rumble-hook.dll'),
            (Join-Path $rumbleRoot 'aitd4-rumble.ini')
        )) {
            if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
                throw "Required installed payload is missing: $required"
            }
        }
        New-Item -ItemType Directory -Path $appRoot -Force | Out-Null
        $ownedFiles = @(Get-OwnedFiles)
        [ordered]@{
            format = 1
            installed_utc = [DateTime]::UtcNow.ToString('o')
            game_executable_sha256 = $supportedExeHash
            files = $ownedFiles
        } | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $appRoot 'ownership.json') -Encoding UTF8

        $previous = Join-Path $appRoot 'backup\previous'
        if (Test-Path -LiteralPath $previous) {
            throw "Previous-install backup already exists: $previous"
        }
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
        } else {
            $preserved = Preserve-CurrentInstall
            Set-Content -LiteralPath (Join-Path $preserved 'README.txt') -Encoding UTF8 -Value @(
                'This folder contains files that differed from the installed overhaul manifest.',
                'They were preserved so uninstall could restore the previous game stack without data loss.'
            )
        }
        $previous = Join-Path $appRoot 'backup\previous'
        if (Test-Path -LiteralPath $previous) { Restore-Previous $previous }
        if (Test-Path -LiteralPath $loaderLog -PathType Leaf) {
            Remove-Item -LiteralPath $loaderLog -Force
        }
        break
    }
}
