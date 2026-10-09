param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('0.6.0-upgrade', '0.6.1-lcd', '0.6.2-repair', '0.6.3-context')]
    [string]$Version
)
$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path -Parent $PSScriptRoot
$taskHistory = Join-Path $taskRoot "history\$Version"
$taskManifest = Get-Content (Join-Path $taskRoot 'history\manifest.json') -Raw | ConvertFrom-Json
$taskRecord = $taskManifest.versions.$Version
foreach ($taskPair in @(@('source.zip', 'source'), @('build.config', 'config'))) {
    $taskFile = Join-Path $taskHistory $taskPair[0]
    $taskExpected = $taskRecord.($taskPair[1])
    if ((Get-Item -LiteralPath $taskFile).Length -ne $taskExpected.bytes -or
        (Get-FileHash -LiteralPath $taskFile -Algorithm SHA256).Hash -ne $taskExpected.sha256) {
        throw "History checksum mismatch: $taskFile"
    }
}
$taskOutput = Join-Path $taskRoot "build-history\$Version"
if (Test-Path -LiteralPath $taskOutput) { throw "Build directory already exists: $taskOutput" }
Add-Type -AssemblyName System.IO.Compression.FileSystem
$taskZip = [IO.Compression.ZipFile]::OpenRead((Join-Path $taskHistory 'source.zip'))
try {
    foreach ($taskEntry in $taskZip.Entries) {
        $taskDestination = [IO.Path]::GetFullPath((Join-Path $taskOutput $taskEntry.FullName))
        if (-not $taskDestination.StartsWith($taskOutput + [IO.Path]::DirectorySeparatorChar,
                [StringComparison]::OrdinalIgnoreCase)) { throw 'Archive path escapes build directory' }
    }
} finally { $taskZip.Dispose() }
Expand-Archive -LiteralPath (Join-Path $taskHistory 'source.zip') -DestinationPath $taskOutput
Copy-Item -LiteralPath (Join-Path $taskHistory 'build.config') -Destination (Join-Path $taskOutput 'sdkconfig')
# Share the installed SDK/compiler; only the small locked components are copied.
Copy-Item -LiteralPath (Join-Path $taskRoot 'managed_components') -Destination $taskOutput -Recurse
$taskOptions = @()
foreach ($taskProperty in $taskRecord.build_options.PSObject.Properties) {
    $taskOptions += @('-D', "$($taskProperty.Name)=$($taskProperty.Value)")
}
& "$PSScriptRoot/idf.ps1" --no-ccache -C $taskOutput -B (Join-Path $taskOutput 'build') `
    -D "SDKCONFIG=$(Join-Path $taskOutput 'sdkconfig')" @taskOptions build
exit $LASTEXITCODE
