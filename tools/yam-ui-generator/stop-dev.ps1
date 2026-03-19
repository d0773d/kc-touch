$ErrorActionPreference = "Stop"

function Get-ProcessMap {
    $all = Get-CimInstance Win32_Process
    $byParent = @{}

    foreach ($proc in $all) {
        if (-not $byParent.ContainsKey($proc.ParentProcessId)) {
            $byParent[$proc.ParentProcessId] = @()
        }
        $byParent[$proc.ParentProcessId] += $proc
    }

    return @{
        All = $all
        ByParent = $byParent
    }
}

function Get-DescendantProcessIds {
    param(
        [int[]]$RootIds,
        [hashtable]$ByParent
    )

    $seen = New-Object System.Collections.Generic.HashSet[int]
    $queue = New-Object System.Collections.Generic.Queue[int]

    foreach ($id in $RootIds) {
        if ($seen.Add($id)) {
            $queue.Enqueue($id)
        }
    }

    while ($queue.Count -gt 0) {
        $current = $queue.Dequeue()
        if ($ByParent.ContainsKey($current)) {
            foreach ($child in $ByParent[$current]) {
                if ($seen.Add([int]$child.ProcessId)) {
                    $queue.Enqueue([int]$child.ProcessId)
                }
            }
        }
    }

    return @($seen)
}

function Stop-ProcessTreeByMatch {
    param(
        [string]$Name,
        [scriptblock]$Predicate
    )

    $map = Get-ProcessMap
    $all = $map.All
    $byParent = $map.ByParent

    $roots = @($all | Where-Object $Predicate)
    if (-not $roots.Count) {
        Write-Host "${Name}: no matching process found."
        return
    }

    $rootIds = @($roots | ForEach-Object { [int]$_.ProcessId })
    $allIds = Get-DescendantProcessIds -RootIds $rootIds -ByParent $byParent | Sort-Object -Descending

    foreach ($processId in $allIds) {
        try {
            Stop-Process -Id $processId -Force -ErrorAction Stop
            Write-Host "${Name}: stopped PID $processId"
        }
        catch {
            if ($_.Exception.Message -match "Cannot find a process") {
                continue
            }
            Write-Warning "${Name}: failed to stop PID ${processId}: $($_.Exception.Message)"
        }
    }
}

function Stop-LauncherWindows {
    $launcherProcs = Get-CimInstance Win32_Process | Where-Object {
        $_.Name -match "^powershell(\.exe)?$" -and
        $_.CommandLine -and (
            $_.CommandLine -match "WindowTitle = 'YamUI Backend'" -or
            $_.CommandLine -match "WindowTitle = 'YamUI Frontend'"
        )
    }

    foreach ($launcher in $launcherProcs) {
        try {
            Stop-Process -Id $launcher.ProcessId -Force -ErrorAction Stop
            Write-Host "Launcher window: stopped PID $($launcher.ProcessId)"
        }
        catch {
            if ($_.Exception.Message -match "Cannot find a process") {
                continue
            }
            Write-Warning "Launcher window: failed to stop PID $($launcher.ProcessId): $($_.Exception.Message)"
        }
    }
}

Stop-ProcessTreeByMatch -Name "YamUI backend" -Predicate {
    $_.CommandLine -and (
        $_.CommandLine -match "yam_ui_generator\.api:app" -or
        $_.CommandLine -match "tools\\yam-ui-generator\\backend"
    )
}

Stop-ProcessTreeByMatch -Name "YamUI frontend" -Predicate {
    $_.CommandLine -and (
        $_.CommandLine -match "vite" -or
        $_.CommandLine -match "tools\\yam-ui-generator\\frontend"
    )
}

Stop-LauncherWindows
