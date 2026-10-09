param([switch]$Apply)
$ErrorActionPreference = 'Stop'
$taskRoot = [IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot)).TrimEnd('\')

function Test-RetainedFile([string]$Relative, $Expected) {
    $taskPath = Join-Path $taskRoot $Relative
    if (-not (Test-Path -LiteralPath $taskPath -PathType Leaf) -or
        (Get-Item -LiteralPath $taskPath).Length -ne $Expected.bytes -or
        (Get-FileHash -LiteralPath $taskPath -Algorithm SHA256).Hash -ne $Expected.sha256) {
        throw "Retained file missing or changed: $Relative"
    }
}

# Verify every irreplaceable retained archive/package before deleting anything.
$taskHistory = Get-Content -LiteralPath (Join-Path $taskRoot 'history\manifest.json') -Raw | ConvertFrom-Json
$taskPackage = Get-Content -LiteralPath (Join-Path $taskRoot 'firmware\latest\manifest.json') -Raw | ConvertFrom-Json
foreach ($taskVersion in $taskHistory.versions.PSObject.Properties) {
    Test-RetainedFile "history\$($taskVersion.Name)\source.zip" $taskVersion.Value.source
    Test-RetainedFile "history\$($taskVersion.Name)\build.config" $taskVersion.Value.config
}
foreach ($taskLegacy in $taskHistory.legacy.PSObject.Properties) {
    Test-RetainedFile "history\legacy\$($taskLegacy.Name)" $taskLegacy.Value
}
foreach ($taskFile in $taskPackage.files.PSObject.Properties) {
    Test-RetainedFile "firmware\latest\$($taskFile.Name)" $taskFile.Value
}

# Fixed workspace-only allowlist. Never derive targets from a shell command.
$taskRelativePaths = @(
    '.local', '.toolchains\tools\dist',
    'build-agent', 'build-agent-noaudio', 'build-host', 'build-host-noaudio',
    'build-history', 'artifacts', 'tools\__pycache__', 'host_tests\__pycache__'
)
$taskPlan = @()
foreach ($taskRelative in $taskRelativePaths) {
    $taskPath = Join-Path $taskRoot $taskRelative
    if (-not (Test-Path -LiteralPath $taskPath)) { continue }
    $taskResolved = (Resolve-Path -LiteralPath $taskPath).ProviderPath
    if ($taskResolved -ne [IO.Path]::GetFullPath($taskPath) -or
        -not $taskResolved.StartsWith($taskRoot + '\', [StringComparison]::OrdinalIgnoreCase)) {
        throw "Target outside workspace: $taskRelative"
    }
    $taskAncestor = $taskResolved
    while ($taskAncestor.Length -ge $taskRoot.Length) {
        if ((Get-Item -LiteralPath $taskAncestor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) {
            throw "Linked ancestor refused: $taskAncestor"
        }
        if ($taskAncestor -eq $taskRoot) { break }
        $taskAncestor = Split-Path -Parent $taskAncestor
    }
    # Inspect each directory before descending, so even nested links are rejected.
    $taskPending = New-Object 'System.Collections.Generic.Stack[string]'
    $taskPending.Push($taskResolved)
    $taskBytes = [long]0
    $taskCount = 0
    while ($taskPending.Count) {
        $taskNode = Get-Item -LiteralPath ($taskPending.Pop()) -Force
        if ($taskNode.Attributes -band [IO.FileAttributes]::ReparsePoint) {
            throw "Linked path refused: $($taskNode.FullName)"
        }
        if ($taskNode.PSIsContainer) {
            foreach ($taskChild in Get-ChildItem -LiteralPath $taskNode.FullName -Force) {
                $taskPending.Push($taskChild.FullName)
            }
        } else { $taskBytes += $taskNode.Length; $taskCount++ }
    }
    $taskPlan += [pscustomobject]@{relative=$taskRelative;path=$taskResolved;bytes=$taskBytes;files=$taskCount}
}
$taskProcesses = @(Get-CimInstance Win32_Process | Where-Object { $_.ProcessId -ne $PID -and $_.CommandLine })
foreach ($taskEntry in $taskPlan) {
    foreach ($taskProcess in $taskProcesses) {
        if ($taskProcess.CommandLine.Replace('/', '\').IndexOf($taskEntry.path + '\', [StringComparison]::OrdinalIgnoreCase) -ge 0) {
            throw "Target is in use by process $($taskProcess.ProcessId) ($($taskProcess.Name)): $($taskEntry.relative)"
        }
    }
}
$taskPlan | Select-Object relative, files, @{Name='MiB';Expression={[math]::Round($_.bytes / 1MB, 2)}} | Format-Table
if (-not $Apply) { Write-Output 'Preview only. Add -Apply to delete the listed folders.'; exit 0 }

$taskResult = [ordered]@{at=(Get-Date -Format o);status='running';device_accessed=$false;deleted=@();failed=@()}
$taskResultPath = Join-Path $taskRoot 'history\cleanup-result.json'
foreach ($taskEntry in $taskPlan) {
    try {
        # Recheck immediately before deletion; all paths were validated above.
        if ((Resolve-Path -LiteralPath $taskEntry.path).ProviderPath -ne $taskEntry.path -or
            ((Get-Item -LiteralPath $taskEntry.path -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) {
            throw 'Target changed during cleanup'
        }
        Remove-Item -LiteralPath $taskEntry.path -Recurse -Force
        if (Test-Path -LiteralPath $taskEntry.path) { throw 'Directory still exists' }
        $taskResult.deleted += [pscustomobject]@{relative=$taskEntry.relative;files=$taskEntry.files;bytes=$taskEntry.bytes}
    } catch {
        $taskResult.failed += [pscustomobject]@{relative=$taskEntry.relative;message=$_.Exception.Message}
        $taskResult.status = 'partial'
        $taskResult | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $taskResultPath -Encoding utf8
        throw
    }
    $taskResult | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $taskResultPath -Encoding utf8
}
$taskResult.status = 'complete'
$taskResult.deleted_bytes = ($taskResult.deleted | Measure-Object bytes -Sum).Sum
$taskResult | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $taskResultPath -Encoding utf8
Write-Output ('Cleanup complete. Removed {0:N2} GiB; source, history, latest firmware and compiler retained.' -f ($taskResult.deleted_bytes / 1GB))
