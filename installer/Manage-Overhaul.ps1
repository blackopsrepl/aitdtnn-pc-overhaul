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
$controllerFileNames = @('dinput8.dll', 'winmm.dll', 'Xidi.32.dll', 'Xidi.ini', 'keys.bin')
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
    foreach ($name in @('audio-restoration', 'renderer', 'rumble', 'version.dll') + $controllerFileNames) {
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
    foreach ($name in $controllerFileNames) {
        $path = Join-Path $gameRoot $name
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { continue }
        $files.Add([ordered]@{
            path = $name
            sha256 = Get-Sha256 $path
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
    if (Test-Path -LiteralPath $appRoot -PathType Container) {
        foreach ($file in Get-ChildItem -LiteralPath $appRoot -Recurse -File) {
            $relativeWithinApp = $file.FullName.Substring($appRoot.Length + 1).Replace('\', '/')
            if ($relativeWithinApp -eq 'ownership.json' -or
                $relativeWithinApp.StartsWith('backup/', [StringComparison]::OrdinalIgnoreCase) -or
                $relativeWithinApp -match '^unins\d+\.(exe|dat|msg)$') {
                continue
            }
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
    if (Test-Path -LiteralPath $appRoot -PathType Container) {
        foreach ($file in Get-ChildItem -LiteralPath $appRoot -Recurse -File) {
            $relativeWithinApp = $file.FullName.Substring($appRoot.Length + 1).Replace('\', '/')
            if ($relativeWithinApp -eq 'ownership.json' -or
                $relativeWithinApp.StartsWith('backup/', [StringComparison]::OrdinalIgnoreCase) -or
                $relativeWithinApp -match '^unins\d+\.(exe|dat|msg)$') {
                continue
            }
            $relative = $file.FullName.Substring($gameRoot.Length + 1).Replace('\', '/')
            if (-not $owned.ContainsKey($relative.ToLowerInvariant())) {
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
    foreach ($name in $controllerFileNames) {
        Move-IfPresent (Join-Path $gameRoot $name) (Join-Path $preserved $name)
    }
    if (Test-Path -LiteralPath $appRoot -PathType Container) {
        $appPreserved = Join-Path $preserved 'aitdtnn-overhaul'
        New-Item -ItemType Directory -Path $appPreserved | Out-Null
        foreach ($item in Get-ChildItem -LiteralPath $appRoot -Force) {
            if ($item.Name -eq 'backup' -or $item.Name -match '^unins\d+\.(exe|dat|msg)$') {
                continue
            }
            Copy-Item -LiteralPath $item.FullName -Destination $appPreserved -Recurse -Force
        }
    }
    return $preserved
}

function Remove-OwnedAppFiles([object[]]$OwnedFiles) {
    $appPrefix = $appRoot.Substring($gameRoot.Length + 1).Replace('\', '/') + '/'
    foreach ($record in $OwnedFiles) {
        $relative = [string]$record.path
        if (-not $relative.StartsWith($appPrefix, [StringComparison]::OrdinalIgnoreCase)) {
            continue
        }
        $path = Join-Path $gameRoot $relative.Replace('/', '\')
        if (Test-Path -LiteralPath $path -PathType Leaf) {
            Remove-Item -LiteralPath $path -Force
        }
    }
    if (Test-Path -LiteralPath (Join-Path $appRoot 'ownership.json') -PathType Leaf) {
        Remove-Item -LiteralPath (Join-Path $appRoot 'ownership.json') -Force
    }
    foreach ($directory in @(
        (Join-Path $appRoot 'licenses\audio-restoration'),
        (Join-Path $appRoot 'licenses'),
        (Join-Path $appRoot 'tools')
    )) {
        if ((Test-Path -LiteralPath $directory -PathType Container) -and
            -not (Get-ChildItem -LiteralPath $directory -Force | Select-Object -First 1)) {
            Remove-Item -LiteralPath $directory -Force
        }
    }
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
        [ordered]@{
            format = 1
            installed_utc = [DateTime]::UtcNow.ToString('o')
            game_executable_sha256 = $supportedExeHash
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
