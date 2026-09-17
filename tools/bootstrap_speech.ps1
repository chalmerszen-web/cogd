$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$dependencyPython = Join-Path $projectRoot '.toolchains/tools/python_env/idf6.1_py3.11_env/Scripts/python.exe'
& $dependencyPython (Join-Path $PSScriptRoot 'verify_dependencies.py')
if ($LASTEXITCODE) { throw 'Restore the pinned third_party files before building' }
