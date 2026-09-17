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
# Use Espressif's existing Python, never modify the system Python installation.
Push-Location $idfRoot
try {
    & .\install.ps1 esp32c3
    if ($LASTEXITCODE) { throw 'ESP-IDF installation failed' }
} finally { Pop-Location }
& (Join-Path $PSScriptRoot 'bootstrap_speech.ps1')
if (-not $?) { throw 'Speech backend setup failed' }
Write-Output 'Ready. Use tools/build_agent.ps1 to build firmware.'
