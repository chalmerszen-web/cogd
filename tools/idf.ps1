$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
# Leave all arguments for idf.py. Set IDF_PATH to select an existing installation
# and optionally IDF_TOOLS_PATH for its tools, without changing -B/-D parsing.
$projectIdfPath = Join-Path $projectRoot '.toolchains\esp-idf'
if (-not $env:IDF_PATH) {
    $env:IDF_PATH = $projectIdfPath
    # A host-wide IDF_TOOLS_PATH can point at an unrelated IDF/Python version.
    # Selecting the private SDK also selects its private tools by default.
    $env:IDF_TOOLS_PATH = Join-Path $projectRoot '.toolchains\tools'
} elseif (-not $env:IDF_TOOLS_PATH -and
    [IO.Path]::GetFullPath($env:IDF_PATH) -eq [IO.Path]::GetFullPath($projectIdfPath)) {
    $env:IDF_TOOLS_PATH = Join-Path $projectRoot '.toolchains\tools'
}
$idfExport = Join-Path $env:IDF_PATH 'export.ps1'
if (-not (Test-Path -LiteralPath $idfExport)) {
    throw 'ESP-IDF export.ps1 not found. Run tools/bootstrap.ps1 or set IDF_PATH to an existing ESP-IDF 6.1 installation.'
}
$idfBootstrapPython = 'C:\Espressif\tools\idf-python\3.11.2'
if (Test-Path -LiteralPath (Join-Path $idfBootstrapPython 'python.exe')) {
    $env:PATH = $idfBootstrapPython + ';' + $env:PATH
}
if (-not $env:HTTPS_PROXY) {
    $systemProxy = & python -c 'import urllib.request; print(urllib.request.getproxies().get("https", ""))'
    if ($systemProxy) { $env:HTTPS_PROXY = $systemProxy; $env:HTTP_PROXY = $systemProxy }
}
. $idfExport
if (-not $?) { throw 'ESP-IDF environment setup failed' }
$taskIdfPython = Join-Path $env:IDF_PYTHON_ENV_PATH 'Scripts\python.exe'
& $taskIdfPython (Join-Path $env:IDF_PATH 'tools\idf.py') @args
exit $LASTEXITCODE
