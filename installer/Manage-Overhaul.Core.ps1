# Safety, hashing, ownership and rollback helpers for Manage-Overhaul.ps1.
# This file expects the validated path variables created by the entry script.
# Keeping destructive filesystem operations here makes their safety boundary easy to audit.
# Paths are canonicalized under the chosen game directory before mutation.
# Ownership hashes distinguish unchanged files (safe to remove) from modified
# files, for which uninstall preserves the entire current stack.

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
        (Join-Path $appRoot 'docs'),
        (Join-Path $appRoot 'tools')
    )) {
        if ((Test-Path -LiteralPath $directory -PathType Container) -and
            -not (Get-ChildItem -LiteralPath $directory -Force | Select-Object -First 1)) {
            Remove-Item -LiteralPath $directory -Force
        }
    }
}
