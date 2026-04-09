$ErrorActionPreference = "Stop"

param(
    [string]$SourcePath,
    [switch]$FromClipboard
)

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoRoot = (Resolve-Path (Join-Path $scriptDir "..\..")).Path
$targetPath = Join-Path $repoRoot "components\ui_schemas\schemas\home.yml"
$backupDir = Join-Path $repoRoot "components\ui_schemas\schemas\_backups"

if (-not (Test-Path $targetPath)) {
    throw "Target schema file not found: $targetPath"
}

if ([string]::IsNullOrWhiteSpace($SourcePath) -and -not $FromClipboard) {
    throw "Provide -SourcePath <yaml-file> or use -FromClipboard."
}

if (-not [string]::IsNullOrWhiteSpace($SourcePath) -and $FromClipboard) {
    throw "Use either -SourcePath or -FromClipboard, not both."
}

$yamlText = $null
$sourceLabel = $null

if ($FromClipboard) {
    Add-Type -AssemblyName PresentationCore
    if (-not [Windows.Clipboard]::ContainsText()) {
        throw "Clipboard does not currently contain text."
    }
    $yamlText = [Windows.Clipboard]::GetText()
    $sourceLabel = "clipboard"
}
else {
    $resolvedSource = (Resolve-Path $SourcePath).Path
    if (-not (Test-Path $resolvedSource)) {
        throw "Source YAML file not found: $SourcePath"
    }
    $yamlText = Get-Content -Raw $resolvedSource
    $sourceLabel = $resolvedSource
}

if ([string]::IsNullOrWhiteSpace($yamlText)) {
    throw "Source YAML content is empty."
}

$trimmed = $yamlText.TrimStart()
if (-not ($trimmed.StartsWith("app:") -or $trimmed.StartsWith("version:") -or $trimmed.StartsWith("screens:") -or $trimmed.StartsWith("styles:") -or $trimmed.StartsWith("state:"))) {
    Write-Warning "The YAML does not look like a full YamUI project root. Continuing anyway."
}

New-Item -ItemType Directory -Force -Path $backupDir | Out-Null
$timestamp = Get-Date -Format "yyyyMMdd-HHmmss"
$backupPath = Join-Path $backupDir "home-$timestamp.yml"
Copy-Item $targetPath $backupPath -Force

$normalizedYaml = $yamlText -replace "`r`n", "`n"
Set-Content -Path $targetPath -Value $normalizedYaml -NoNewline

Write-Host "Installed generated YamUI schema into:"
Write-Host "  $targetPath"
Write-Host ""
Write-Host "Backup saved to:"
Write-Host "  $backupPath"
Write-Host ""
Write-Host "Source:"
Write-Host "  $sourceLabel"
Write-Host ""
Write-Host "Next steps:"
Write-Host "  cd $repoRoot"
Write-Host "  idf.py build"
Write-Host "  idf.py -p COM11 flash monitor"
