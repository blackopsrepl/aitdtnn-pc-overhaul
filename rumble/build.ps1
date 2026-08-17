[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
& (Join-Path $PSScriptRoot 'build.cmd')
if ($LASTEXITCODE -ne 0) {
    throw "Rumble build or test failed with exit code $LASTEXITCODE."
}

$manifest = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'component.template.json') -Raw |
    ConvertFrom-Json
$manifest.version = (Get-Content -LiteralPath (Join-Path $PSScriptRoot 'VERSION') -Raw).Trim()
foreach ($entry in $manifest.payload) {
    $source = Join-Path $PSScriptRoot ([string]$entry.source).Replace('/', '\')
    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
        throw "Component payload is missing: $source"
    }
    $entry | Add-Member -NotePropertyName sha256 -NotePropertyValue `
        (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash.ToLowerInvariant() -Force
}
$manifest | ConvertTo-Json -Depth 8 |
    Set-Content -LiteralPath (Join-Path $PSScriptRoot 'bin\component.json') -Encoding utf8
