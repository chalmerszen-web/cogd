param([string]$Port='COM3', [string]$BuildDir='build', [switch]$SkipAssets)
$ErrorActionPreference='Stop'
$p4Project=Split-Path -Parent $PSScriptRoot
$p4Build=Join-Path $p4Project $BuildDir
$p4Files=@('bootloader/bootloader.bin','partition_table/partition-table.bin','cogd_p4.bin')
foreach($p4File in $p4Files) {
    if(!(Test-Path -LiteralPath (Join-Path $p4Build $p4File))) { throw "Missing build artifact: $p4File" }
}
$p4FlashArgs=@('-m','esptool','--chip','esp32p4','--port',$Port,'--baud','921600',
    '--before','default-reset','--after','hard-reset','write-flash',
    '--flash-mode','dio','--flash-size','16MB','--flash-freq','80m',
    '0x2000',(Join-Path $p4Build $p4Files[0]),
    '0x8000',(Join-Path $p4Build $p4Files[1]),
    '0x10000',(Join-Path $p4Build $p4Files[2]))
if (-not $SkipAssets) {
    $p4Assets=Join-Path $p4Build 'p4-assets.bin'
    if (-not (Test-Path -LiteralPath $p4Assets)) {
        throw 'Missing p4-assets.bin. Run tools/build.ps1 for this P4 project; use -SkipAssets only for a legacy package when compatible font assets are already installed.'
    }
    $p4FlashArgs += @('0x880000',$p4Assets)
}
# Only these image ranges are written; NVS, context, and the remaining video cache
# are preserved. -SkipAssets also preserves the currently installed font region.
& python @p4FlashArgs
exit $LASTEXITCODE
