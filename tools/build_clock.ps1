param([string]$BuildDir = 'build-clock', [switch]$LegacyWake)
& "$PSScriptRoot/build_voice_test.ps1" -BuildDir $BuildDir -Clock -LegacyWake:$LegacyWake
exit $LASTEXITCODE
