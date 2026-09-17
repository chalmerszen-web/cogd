param([string]$Port)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$python = Join-Path $projectRoot '.toolchains\tools\python_env\idf6.1_py3.11_env\Scripts\python.exe'
Push-Location $projectRoot
try {
    if ($Port) { & $python .\tools\provision_secret.py --port $Port }
    else { & $python .\tools\provision_secret.py }
    $provisionExit = $LASTEXITCODE
    Read-Host 'Press Enter to close this configuration window' | Out-Null
} finally { Pop-Location }
exit $provisionExit
