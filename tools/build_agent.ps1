param([string]$BuildDir = 'build-agent', [switch]$NoAudio)
$ErrorActionPreference = 'Stop'
$taskProject = Split-Path -Parent $PSScriptRoot
Push-Location $taskProject
try {
    # One supported speech baseline; audio-off remains a small portability check.
    $speechEnabled = if ($NoAudio) { 'OFF' } else { 'ON' }
    $configDefaults = 'sdkconfig.defaults;sdkconfig.upgrade.defaults'
    if ($NoAudio) { $configDefaults += ';sdkconfig.noaudio.defaults' }
    & "$PSScriptRoot/idf.ps1" -B $BuildDir -D "SDKCONFIG=$BuildDir/sdkconfig" `
        -D "SDKCONFIG_DEFAULTS=$configDefaults" `
        -D "AGENT_MIC_OVERSAMPLE=$speechEnabled" -D AGENT_MIC_ATTEN_DB=12 `
        -D "AGENT_KEYWORD_VERIFY=$speechEnabled" -D "AGENT_BACKGROUND_VERIFY=$speechEnabled" `
        -D "AGENT_PACKED_CLIP=$speechEnabled" build
    $taskResult = $LASTEXITCODE
} finally { Pop-Location }
exit $taskResult
