[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)] [string]$RetailExe,
    [Parameter(Mandatory = $true)] [string]$FifteenSlotExe,
    # Optional Steam (app 259190) alone4.exe. When given, it is also proven to be
    # the retail CD image with only the known CD-check bytes changed, which is
    # why every module reuses the retail CD addresses for it.
    [string]$SteamExe
)

$ErrorActionPreference = 'Stop'
$expected = @{
    'retail-cd' = '320908AF4CE5C724B60A7EEA6A5AADE737D51D65AEE8506744FCE6E6DD0143E0'
    '15-slot-no-cd' = '5668118E0E19D569986500A1C805A85397C8681E7B672B49A68645462ECCC672'
    'steam' = '228123A2E5620AD1976F83B867513B761CA824610E0BF73678C5BECEBA6BD815'
}

$entries = @(@{ Name = 'retail-cd'; Path = $RetailExe }, @{ Name = '15-slot-no-cd'; Path = $FifteenSlotExe })
if ($SteamExe) { $entries += @{ Name = 'steam'; Path = $SteamExe } }

foreach ($entry in $entries) {
    $path = (Resolve-Path -LiteralPath $entry.Path).Path
    $actual = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash
    if ($actual -ne $expected[$entry.Name]) {
        throw "$($entry.Name) hash mismatch: $actual"
    }
    Write-Output "$($entry.Name): verified $actual"
}

if ($SteamExe) {
    # File offsets equal RVAs in this image (raw and virtual section layouts match).
    # 0x8D722: je after the CD-file CreateFileA -> nop nop
    # 0x8DB28: jne on the cached CD drive letter -> jmp
    # 0xC708C: "Please insert CD 3 to continue" -> "Loading, please wait..."
    $allowed = @(@(0x8D722, 2), @(0x8DB28, 1), @(0xC708C, 30))
    $retail = [IO.File]::ReadAllBytes((Resolve-Path -LiteralPath $RetailExe).Path)
    $steam = [IO.File]::ReadAllBytes((Resolve-Path -LiteralPath $SteamExe).Path)
    if ($retail.Length -ne $steam.Length) {
        throw "steam image size $($steam.Length) differs from retail-cd $($retail.Length)"
    }
    $changed = 0
    for ($offset = 0; $offset -lt $retail.Length; $offset++) {
        if ($retail[$offset] -eq $steam[$offset]) { continue }
        $changed++
        $known = $false
        foreach ($range in $allowed) {
            if ($offset -ge $range[0] -and $offset -lt $range[0] + $range[1]) { $known = $true; break }
        }
        if (-not $known) {
            throw ('steam differs from retail-cd outside the known CD-check patch at 0x{0:X}' -f $offset)
        }
    }
    Write-Output "steam: retail-cd layout verified ($changed bytes differ, all inside the CD-check patch)"
}
