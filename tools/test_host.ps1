$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
Push-Location $projectRoot
try {
    & wsl -d Ubuntu -- sh -lc 'cmake -S host_tests -B build-host -G Ninja -DCMAKE_BUILD_TYPE=Debug && cmake --build build-host && ctest --test-dir build-host --output-on-failure'
    $testExit = $LASTEXITCODE
} finally { Pop-Location }
exit $testExit
