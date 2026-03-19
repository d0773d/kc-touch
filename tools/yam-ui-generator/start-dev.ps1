param(
    [string]$ApiBaseUrl,
    [string]$BackendBindHost = "0.0.0.0",
    [string]$FrontendBindHost = "0.0.0.0",
    [int]$BackendPort = 8000
)

$ErrorActionPreference = "Stop"

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$backendDir = Join-Path $scriptDir "backend"
$frontendDir = Join-Path $scriptDir "frontend"

if (-not (Test-Path $backendDir)) {
    throw "Backend directory not found: $backendDir"
}

if (-not (Test-Path $frontendDir)) {
    throw "Frontend directory not found: $frontendDir"
}

function Get-PreferredLocalIPv4 {
    $candidates = Get-NetIPAddress -AddressFamily IPv4 -ErrorAction SilentlyContinue | Where-Object {
        $_.IPAddress -ne "127.0.0.1" -and
        $_.PrefixOrigin -ne "WellKnown" -and
        $_.IPAddress -notlike "169.254.*"
    }

    $preferred = $candidates | Select-Object -First 1
    if ($preferred) {
        return $preferred.IPAddress
    }

    return "127.0.0.1"
}

if ([string]::IsNullOrWhiteSpace($ApiBaseUrl)) {
    $localIp = Get-PreferredLocalIPv4
    $ApiBaseUrl = "http://${localIp}:${BackendPort}"
}

$backendCommand = @"
`$Host.UI.RawUI.WindowTitle = 'YamUI Backend'
Set-Location '$backendDir'
poetry run uvicorn yam_ui_generator.api:app --reload --host $BackendBindHost --port $BackendPort
"@

$frontendCommand = @"
`$Host.UI.RawUI.WindowTitle = 'YamUI Frontend'
Set-Location '$frontendDir'
`$env:VITE_API_BASE_URL = '$ApiBaseUrl'
npx.cmd vite --host $FrontendBindHost
"@

function Start-YamUITerminal {
    param(
        [string]$Title,
        [string]$Command
    )

    $wt = Get-Command wt.exe -ErrorAction SilentlyContinue
    if ($wt) {
        Start-Process $wt.Source -ArgumentList @(
            "new-tab",
            "--title", $Title,
            "--suppressApplicationTitle",
            "powershell.exe",
            "-NoProfile",
            "-NoExit",
            "-Command", $Command
        )
        return
    }

    Start-Process powershell -ArgumentList @(
        "-NoProfile",
        "-NoExit",
        "-Command",
        $Command
    )
}

Write-Host "Starting YamUI backend with Poetry..."
Start-YamUITerminal -Title "YamUI Backend" -Command $backendCommand

Start-Sleep -Seconds 2

Write-Host "Starting YamUI frontend..."
Start-YamUITerminal -Title "YamUI Frontend" -Command $frontendCommand

Write-Host ""
Write-Host "Backend bind: ${BackendBindHost}:${BackendPort}"
Write-Host "Frontend bind: ${FrontendBindHost}:5173"
Write-Host "Frontend API base: $ApiBaseUrl"
