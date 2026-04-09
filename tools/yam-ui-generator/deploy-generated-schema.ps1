param(
    [string]$SourcePath,
    [switch]$FromClipboard,
    [switch]$BuildOnly,
    [switch]$NoFlash,
    [string]$Port = "COM11"
)

$ErrorActionPreference = "Stop"

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoRoot = (Resolve-Path (Join-Path $scriptDir "..\..")).Path
$installScript = Join-Path $scriptDir "use-generated-schema.ps1"

if (-not (Test-Path $installScript)) {
    throw "Install script not found: $installScript"
}

if ([string]::IsNullOrWhiteSpace($SourcePath) -and -not $FromClipboard) {
    throw "Provide -SourcePath <yaml-file> or use -FromClipboard."
}

if (-not [string]::IsNullOrWhiteSpace($SourcePath) -and $FromClipboard) {
    throw "Use either -SourcePath or -FromClipboard, not both."
}

$installArgs = @()
if ($FromClipboard) {
    $installArgs += "-FromClipboard"
}
else {
    $installArgs += @("-SourcePath", $SourcePath)
}

Write-Host "Installing generated schema into embedded home.yml..."
& $installScript @installArgs

Write-Host ""
Write-Host "Building firmware..."
Push-Location $repoRoot
try {
    & idf.py build

    if (-not $BuildOnly -and -not $NoFlash) {
        Write-Host ""
        Write-Host "Flashing and opening monitor on $Port..."
        & idf.py -p $Port flash monitor
    }
    elseif ($NoFlash) {
        Write-Host ""
        Write-Host "Build complete. Flash skipped by request."
    }
    else {
        Write-Host ""
        Write-Host "Build complete. Flash skipped because -BuildOnly was used."
    }
}
finally {
    Pop-Location
}
