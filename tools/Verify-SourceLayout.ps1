[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$extensions = @(
    '.c', '.cpp', '.h', '.hpp', '.inc', '.inl',
    '.py', '.ps1', '.psm1', '.iss', '.cmd', '.bat',
    '.vert', '.frag', '.glsl', '.json', '.md'
)

# Legal texts must remain verbatim. Imported source and generated artifacts are
# maintained upstream or recreated by the build, so neither belongs to this rule.
$excludedPrefixes = @(
    '.git/', 'build/', 'third_party/',
    'audio-restoration/third_party/', 'audio-restoration/licenses/'
)
$excludedFiles = @('LICENSE.txt', 'audio-restoration/LICENSE.txt')
$violations = New-Object 'System.Collections.Generic.List[object]'

foreach ($path in & rg --files $repo) {
    $relative = [IO.Path]::GetRelativePath($repo, $path).Replace('\', '/')
    if ($relative -in $excludedFiles) { continue }
    if ($excludedPrefixes.Where({ $relative.StartsWith($_, [StringComparison]::OrdinalIgnoreCase) })) {
        continue
    }
    if ([IO.Path]::GetExtension($relative).ToLowerInvariant() -notin $extensions) { continue }
    $lineCount = @(Get-Content -LiteralPath $path).Count
    if ($lineCount -ge 300) {
        $violations.Add([pscustomobject]@{ Lines = $lineCount; File = $relative })
    }
}

if ($violations.Count) {
    $details = ($violations | Sort-Object Lines -Descending | Format-Table -AutoSize | Out-String).Trim()
    throw "First-party files must stay below 300 physical lines:`n$details"
}

Write-Host 'Source layout verified: every governed first-party file is below 300 physical lines.'
