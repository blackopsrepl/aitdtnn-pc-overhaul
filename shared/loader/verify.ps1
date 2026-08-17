[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$ProxyPath,

    [string]$GameExe
)

$ErrorActionPreference = 'Stop'
$expectedHash = '5668118E0E19D569986500A1C805A85397C8681E7B672B49A68645462ECCC672'
$expectedPrologue = [byte[]](0x55, 0x8B, 0xEC, 0x6A, 0xFF)
$expectedExports = @(
    'GetFileVersionInfoA',
    'GetFileVersionInfoByHandle',
    'GetFileVersionInfoExA',
    'GetFileVersionInfoExW',
    'GetFileVersionInfoSizeA',
    'GetFileVersionInfoSizeExA',
    'GetFileVersionInfoSizeExW',
    'GetFileVersionInfoSizeW',
    'GetFileVersionInfoW',
    'VerFindFileA',
    'VerFindFileW',
    'VerInstallFileA',
    'VerInstallFileW',
    'VerLanguageNameA',
    'VerLanguageNameW',
    'VerQueryValueA',
    'VerQueryValueW'
)

function Find-Dumpbin {
    $command = Get-Command dumpbin.exe -ErrorAction SilentlyContinue
    if ($command) { return $command.Source }

    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path -LiteralPath $vswhere)) {
        throw 'dumpbin.exe and vswhere.exe were not found.'
    }
    $visualStudio = & $vswhere -latest -products * `
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
        -property installationPath
    $candidate = Get-ChildItem -LiteralPath (Join-Path $visualStudio 'VC\Tools\MSVC') `
        -Filter dumpbin.exe -Recurse | Where-Object {
            $_.FullName -match '\\Hostx64\\x64\\dumpbin\.exe$'
        } | Sort-Object FullName -Descending | Select-Object -First 1
    if (-not $candidate) { throw 'Could not locate dumpbin.exe.' }
    return $candidate.FullName
}

function Read-Exports([string]$Path, [string]$Dumpbin) {
    $result = @{}
    foreach ($line in (& $Dumpbin /nologo /exports $Path)) {
        if ($line -match '^\s+(\d+)\s+[0-9A-F]+\s+(?:[0-9A-F]+\s+)?([A-Za-z][A-Za-z0-9]+)(?:\s|$)') {
            $result[[int]$Matches[1]] = $Matches[2]
        }
    }
    return $result
}

function Read-EntrypointBytes([string]$Path, [int]$Count) {
    $bytes = [System.IO.File]::ReadAllBytes($Path)
    if ($bytes.Length -lt 0x40 -or $bytes[0] -ne 0x4D -or $bytes[1] -ne 0x5A) {
        throw "$Path is not a valid PE file."
    }
    $peOffset = [BitConverter]::ToInt32($bytes, 0x3C)
    if ($peOffset -lt 0 -or $peOffset + 24 -ge $bytes.Length -or
        [BitConverter]::ToUInt32($bytes, $peOffset) -ne 0x00004550) {
        throw "$Path has an invalid PE header."
    }
    $sectionCount = [BitConverter]::ToUInt16($bytes, $peOffset + 6)
    $optionalSize = [BitConverter]::ToUInt16($bytes, $peOffset + 20)
    $optionalOffset = $peOffset + 24
    if ([BitConverter]::ToUInt16($bytes, $optionalOffset) -ne 0x10B) {
        throw "$Path is not a PE32 executable."
    }
    $entryRva = [BitConverter]::ToUInt32($bytes, $optionalOffset + 16)
    $sectionOffset = $optionalOffset + $optionalSize
    for ($index = 0; $index -lt $sectionCount; $index++) {
        $header = $sectionOffset + $index * 40
        $virtualSize = [BitConverter]::ToUInt32($bytes, $header + 8)
        $virtualAddress = [BitConverter]::ToUInt32($bytes, $header + 12)
        $rawSize = [BitConverter]::ToUInt32($bytes, $header + 16)
        $rawOffset = [BitConverter]::ToUInt32($bytes, $header + 20)
        $span = [Math]::Max($virtualSize, $rawSize)
        if ($entryRva -ge $virtualAddress -and $entryRva + $Count -le $virtualAddress + $span) {
            $fileOffset = $rawOffset + ($entryRva - $virtualAddress)
            return [byte[]]$bytes[$fileOffset..($fileOffset + $Count - 1)]
        }
    }
    throw 'Could not map the executable entrypoint RVA to a file offset.'
}

if (-not (Test-Path -LiteralPath $ProxyPath -PathType Leaf)) {
    throw "Proxy DLL not found: $ProxyPath"
}
$ProxyPath = (Resolve-Path -LiteralPath $ProxyPath).Path
$dumpbin = Find-Dumpbin

$headers = & $dumpbin /nologo /headers $ProxyPath
if (-not ($headers -match '^\s*14C machine \(x86\)')) {
    throw 'The proxy is not an x86 PE image.'
}

$actualExports = Read-Exports $ProxyPath $dumpbin
if ($actualExports.Count -ne $expectedExports.Count) {
    throw "Expected 17 exports but found $($actualExports.Count)."
}
for ($index = 0; $index -lt $expectedExports.Count; $index++) {
    $ordinal = $index + 1
    if ($actualExports[$ordinal] -ne $expectedExports[$index]) {
        throw "Export ordinal $ordinal is '$($actualExports[$ordinal])'; expected '$($expectedExports[$index])'."
    }
}

$systemVersion = if ([Environment]::Is64BitOperatingSystem) {
    Join-Path $env:WINDIR 'SysWOW64\version.dll'
} else {
    Join-Path $env:WINDIR 'System32\version.dll'
}
$systemExports = Read-Exports $systemVersion $dumpbin
for ($index = 0; $index -lt $expectedExports.Count; $index++) {
    $ordinal = $index + 1
    if ($systemExports[$ordinal] -ne $expectedExports[$index]) {
        throw "System version.dll export mismatch at ordinal $ordinal."
    }
}

$dependencies = @(& $dumpbin /nologo /dependents $ProxyPath | ForEach-Object {
    if ($_ -match '^\s+([A-Za-z0-9._-]+\.dll)\s*$') { $Matches[1].ToUpperInvariant() }
} | Where-Object { $_ } | Sort-Object -Unique)
$allowedDependencies = @('KERNEL32.DLL', 'USER32.DLL')
$unexpected = @($dependencies | Where-Object { $_ -notin $allowedDependencies })
if ($unexpected.Count -ne 0) {
    throw "Unexpected proxy dependencies: $($unexpected -join ', ')"
}
foreach ($forbidden in @('VCRUNTIME140.DLL', 'MSVCP140.DLL', 'UCRTBASE.DLL')) {
    if ($forbidden -in $dependencies) { throw "Dynamic CRT dependency found: $forbidden" }
}

if ($GameExe) {
    if (-not (Test-Path -LiteralPath $GameExe -PathType Leaf)) {
        throw "Game executable not found: $GameExe"
    }
    $actualHash = (Get-FileHash -LiteralPath $GameExe -Algorithm SHA256).Hash
    if ($actualHash -ne $expectedHash) {
        throw "Unsupported game SHA-256: $actualHash"
    }
    $actualPrologue = Read-EntrypointBytes $GameExe $expectedPrologue.Count
    if ([BitConverter]::ToString($actualPrologue) -ne [BitConverter]::ToString($expectedPrologue)) {
        throw "Unsupported game entrypoint prologue: $([BitConverter]::ToString($actualPrologue))"
    }
    Write-Host "Game SHA-256: $actualHash"
    Write-Host "Game entrypoint prologue: $([BitConverter]::ToString($actualPrologue))"
}

$proxyHash = (Get-FileHash -LiteralPath $ProxyPath -Algorithm SHA256).Hash
Write-Host 'Verified x86 /MT proxy exports: 17/17, ordinals 1-17.'
Write-Host "Dependencies: $($dependencies -join ', ')"
Write-Host "Proxy SHA-256: $proxyHash"
