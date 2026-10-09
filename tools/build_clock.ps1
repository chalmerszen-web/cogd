param([string]$BuildDir = 'build-clock')
& "$PSScriptRoot/build_voice_test.ps1" -BuildDir $BuildDir -Clock
exit $LASTEXITCODE
