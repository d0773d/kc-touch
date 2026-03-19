$ErrorActionPreference = "Stop"

$targets = @(
    @{
        Name = "YamUI backend"
        Match = "uvicorn yam_ui_generator.api:app --reload"
    },
    @{
        Name = "YamUI frontend"
        Match = "vite"
    }
)

foreach ($target in $targets) {
    $procs = Get-CimInstance Win32_Process | Where-Object {
        $_.Name -match "^(python|python3|node|npm|powershell)(\.exe)?$" -and
        $_.CommandLine -and
        $_.CommandLine.Contains($target.Match)
    }

    if (-not $procs) {
        Write-Host "$($target.Name): no matching process found."
        continue
    }

    foreach ($proc in $procs) {
        try {
            Stop-Process -Id $proc.ProcessId -Force
            Write-Host "$($target.Name): stopped PID $($proc.ProcessId)"
        }
        catch {
            Write-Warning "$($target.Name): failed to stop PID $($proc.ProcessId): $($_.Exception.Message)"
        }
    }
}
