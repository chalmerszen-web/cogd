param([string]$BuildDir = 'build', [switch]$Reconfigure)
$ErrorActionPreference = 'Stop'
$p4Project = Split-Path -Parent $PSScriptRoot
$cogdProject = Split-Path -Parent $p4Project
$p4Options = @{
    AGENT_BOARD_P4='ON'; AGENT_APP_LTO='ON'; AGENT_KWS_C11='ON'
    AGENT_KWS_TRAINED='ON'; AGENT_KWS_CHANNELS='48'; AGENT_KWS_VERIFIED='ON'
    AGENT_KWS_VERIFIED_DIR=(Join-Path $cogdProject 'components/kws_c11/models/voice_test')
    AGENT_KWS_FUSION='OFF'; AGENT_KWS_SILENCE_PRIME='ON'; AGENT_KWS_STABLE_WAKE='OFF'
    AGENT_TRAINED_WAKE_THRESHOLD='740'; AGENT_KEYWORD_VERIFY='OFF'; AGENT_BACKGROUND_VERIFY='ON'
    AGENT_PACKED_CLIP='ON'; AGENT_MIC_OVERSAMPLE='OFF'; AGENT_MIC_EXACT_CLOCK='OFF'
    AGENT_WS_INCREMENTAL='ON'; AGENT_ISOLATED_ASR='ON'; AGENT_TEXT_PREFETCH='ON'
    AGENT_LOCAL_FIRST_PREFETCH='ON'; AGENT_REQUEST_SCRATCH_COMPACT='ON'
    AGENT_CONTEXTUAL_CACHED_ACK='ON'; AGENT_VOICE_HTTP_RELEASE='ON'; AGENT_VOICE_HTTP_FORGET='ON'
    AGENT_VOICE_SCRATCH_LOANS='ON'; AGENT_USER_TEST='ON'; AGENT_CLOCK_BOOT='OFF'
}
$p4Args = @('--no-ccache','-B',$BuildDir)
if ($Reconfigure -or !(Test-Path (Join-Path $p4Project "$BuildDir/CMakeCache.txt"))) {
    $p4Args += @('-D',"SDKCONFIG=$BuildDir/sdkconfig",'-D','SDKCONFIG_DEFAULTS=sdkconfig.defaults')
    foreach ($p4Name in ($p4Options.Keys | Sort-Object)) { $p4Args += @('-D',"$p4Name=$($p4Options[$p4Name])") }
}
Push-Location $p4Project
try { & (Join-Path $cogdProject 'tools/idf.ps1') @p4Args build; $p4Exit=$LASTEXITCODE }
finally { Pop-Location }
exit $p4Exit
