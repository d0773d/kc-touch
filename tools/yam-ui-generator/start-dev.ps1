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

$backendCommand = @"
Set-Location '$backendDir'
poetry run uvicorn yam_ui_generator.api:app --reload
"@

$frontendCommand = @"
Set-Location '$frontendDir'
npm run dev
"@

Write-Host "Starting YamUI backend with Poetry..."
Start-Process powershell -ArgumentList @(
    "-NoExit",
    "-Command",
    $backendCommand
)

Start-Sleep -Seconds 2

Write-Host "Starting YamUI frontend..."
Start-Process powershell -ArgumentList @(
    "-NoExit",
    "-Command",
    $frontendCommand
)

Write-Host ""
Write-Host "Backend:  http://127.0.0.1:8000"
Write-Host "Frontend: http://localhost:5173"
