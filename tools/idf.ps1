$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$env:IDF_TOOLS_PATH = Join-Path $projectRoot '.toolchains\tools'
$env:IDF_PATH = Join-Path $projectRoot '.toolchains\esp-idf'
$env:PATH = 'C:\Espressif\tools\idf-python\3.11.2;' + $env:PATH
if (-not $env:HTTPS_PROXY) {
    $systemProxy = & python -c 'import urllib.request; print(urllib.request.getproxies().get("https", ""))'
    if ($systemProxy) { $env:HTTPS_PROXY = $systemProxy; $env:HTTP_PROXY = $systemProxy }
}
. (Join-Path $env:IDF_PATH 'export.ps1')
if (-not $?) { throw 'ESP-IDF environment setup failed' }
$taskIdfPython = Join-Path $env:IDF_PYTHON_ENV_PATH 'Scripts\python.exe'
& $taskIdfPython (Join-Path $env:IDF_PATH 'tools\idf.py') @args
exit $LASTEXITCODE
