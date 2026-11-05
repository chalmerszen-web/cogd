param(
    [ValidateNotNullOrEmpty()]
    [ValidateSet('esp32c3', 'esp32p4', 'esp32s3')]
    [string[]]$Targets = @('esp32c3', 'esp32p4')
)
# esp32s3 installs its toolchain only; it does not add an S3 board firmware.
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$toolRoot = Join-Path $projectRoot '.toolchains'
$idfRoot = Join-Path $toolRoot 'esp-idf'
$env:IDF_TOOLS_PATH = Join-Path $toolRoot 'tools'
$pythonRoot = 'C:\Espressif\tools\idf-python\3.11.2'
if (Test-Path -LiteralPath (Join-Path $pythonRoot 'python.exe')) {
    $env:PATH = $pythonRoot + ';' + $env:PATH
    if (-not $env:HTTPS_PROXY) {
        $systemProxy = & (Join-Path $pythonRoot 'python.exe') -c 'import urllib.request; print(urllib.request.getproxies().get("https", ""))'
        if ($systemProxy) { $env:HTTPS_PROXY = $systemProxy; $env:HTTP_PROXY = $systemProxy }
    }
}
New-Item -ItemType Directory -Force -Path $toolRoot | Out-Null
if (-not (Test-Path -LiteralPath (Join-Path $idfRoot 'export.ps1'))) {
    git -c core.longpaths=true clone --depth 1 --branch v6.1 --recursive --shallow-submodules https://github.com/espressif/esp-idf.git $idfRoot
    if ($LASTEXITCODE) { throw 'ESP-IDF clone failed' }
}
# ESP-IDF creates its own Python environment; firmware targets are selected later.
Push-Location $idfRoot
try {
    & .\install.ps1 ($Targets -join ',')
    if ($LASTEXITCODE) { throw 'ESP-IDF installation failed' }
} finally { Pop-Location }
& python (Join-Path $PSScriptRoot 'verify_dependencies.py')
if ($LASTEXITCODE) { throw 'Restore the pinned third_party files before building' }
Write-Output ('Ready. Installed toolchains: ' + ($Targets -join ', '))
Write-Output 'Build C3 with tools/build_clock.ps1 or P4 with ESP32-P4-WIFI6-Touch-LCD-3.5/tools/build.ps1.'
if ($Targets -contains 'esp32s3') {
    Write-Output 'ESP32-S3 toolchain installed only; S3 board adaptation and validation are still required.'
}
