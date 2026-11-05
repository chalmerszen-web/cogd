param(
    [Parameter(Mandatory=$true)]
    [ValidateSet('esp-hi-c3','waveshare-p4-3.5')]
    [string]$Board,
    [string]$BuildDir,
    [switch]$Reconfigure
)
$ErrorActionPreference = 'Stop'
$cogdRoot = Split-Path -Parent $PSScriptRoot
# Each board owns its configuration, partitions and output directory. Never
# implement target selection by reusing another chip's CMake cache.
switch ($Board) {
    'esp-hi-c3' {
        if (!$BuildDir) { $BuildDir = 'build-c3-xiaozhi' }
        & (Join-Path $PSScriptRoot 'build_clock.ps1') -BuildDir $BuildDir
    }
    'waveshare-p4-3.5' {
        if (!$BuildDir) { $BuildDir = 'build-p4' }
        & (Join-Path $cogdRoot 'ESP32-P4-WIFI6-Touch-LCD-3.5/tools/build.ps1') -BuildDir $BuildDir -Reconfigure:$Reconfigure
    }
}
exit $LASTEXITCODE
