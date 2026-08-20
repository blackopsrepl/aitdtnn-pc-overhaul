[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)] [string]$RetailExe,
    [Parameter(Mandatory = $true)] [string]$FifteenSlotExe
)

$ErrorActionPreference = 'Stop'
$expected = @{
    'retail-cd' = '320908AF4CE5C724B60A7EEA6A5AADE737D51D65AEE8506744FCE6E6DD0143E0'
    '15-slot-no-cd' = '5668118E0E19D569986500A1C805A85397C8681E7B672B49A68645462ECCC672'
}

foreach ($entry in @(@{ Name = 'retail-cd'; Path = $RetailExe }, @{ Name = '15-slot-no-cd'; Path = $FifteenSlotExe })) {
    $path = (Resolve-Path -LiteralPath $entry.Path).Path
    $actual = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash
    if ($actual -ne $expected[$entry.Name]) {
        throw "$($entry.Name) hash mismatch: $actual"
    }
    Write-Output "$($entry.Name): verified $actual"
}
