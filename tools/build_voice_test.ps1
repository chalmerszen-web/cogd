param([string]$BuildDir = 'build-voice-test', [switch]$Clock)
$ErrorActionPreference = 'Stop'
$taskProject = Split-Path -Parent $PSScriptRoot
$taskModels = Join-Path $taskProject 'components/kws_c11/models/voice_test'
Push-Location $taskProject
try {
    $taskOptions = @{
        AGENT_USER_TEST='ON'; AGENT_APP_LTO='ON'; AGENT_KWS_C11='ON'
        AGENT_CLOCK_BOOT=$(if ($Clock) {'ON'} else {'OFF'})
        AGENT_KWS_TRAINED='ON'; AGENT_KWS_CHANNELS='48'; AGENT_KWS_VERIFIED='ON'
        AGENT_KWS_VERIFIED_DIR=$taskModels; AGENT_KWS_FUSION='OFF'; AGENT_KWS_SILENCE_PRIME='ON'
        AGENT_KWS_STABLE_WAKE='OFF'; AGENT_TRAINED_WAKE_THRESHOLD='740'
        AGENT_KEYWORD_VERIFY='OFF'; AGENT_BACKGROUND_VERIFY='ON'; AGENT_PACKED_CLIP='ON'
        AGENT_MIC_OVERSAMPLE='ON'; AGENT_MIC_EXACT_CLOCK='ON'; AGENT_MIC_ATTEN_DB='12'
        AGENT_WS_INCREMENTAL='ON'; AGENT_ISOLATED_ASR='ON'; AGENT_TEXT_PREFETCH='ON'
        AGENT_LOCAL_FIRST_PREFETCH='ON'; AGENT_REQUEST_SCRATCH_COMPACT='ON'
        AGENT_CONTEXTUAL_CACHED_ACK='ON'; AGENT_VOICE_HTTP_RELEASE='ON'; AGENT_VOICE_HTTP_FORGET='ON'
        AGENT_VOICE_SCRATCH_LOANS='ON'
        AGENT_TLS_COOPERATE='OFF'; AGENT_TLS_ASYM_PERF='OFF'; AGENT_TLS_PUBLIC_PERF='OFF'
        AGENT_TLS_TX_COMPACT='OFF'; AGENT_TLS_PHASE_TRACE='OFF'; AGENT_VOICE_HEAP_DIAGNOSTICS='OFF'
        AGENT_WS_TIMING='OFF'; AGENT_WS_CONNECT_TRACE='OFF'; AGENT_RT_DIAGNOSTICS='OFF'
        AGENT_ENDPOINT_TRACE='OFF'; AGENT_CAPTURE_PROBE='OFF'; AGENT_KEYWORD_PCM='OFF'
        AGENT_WS_OWNER_PROBE='OFF'; AGENT_HANDOFF_PROBE='OFF'
        AGENT_KWS_VERIFIED_RESOURCE_ONLY='OFF'; AGENT_KWS_WIDE_PROBE='OFF'
        AGENT_KWS_GRU_RESOURCE_ONLY='OFF'; AGENT_KWS_GRU64_RESOURCE_ONLY='OFF'; AGENT_KWS_GRU_Q6_RESOURCE_ONLY='OFF'
    }
    $taskArgs = @('--no-ccache','-B',$BuildDir,'-D',"SDKCONFIG=$BuildDir/sdkconfig",'-D',
        'SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.upgrade.defaults;sdkconfig.voice-test.defaults')
    foreach ($taskName in ($taskOptions.Keys | Sort-Object)) {
        $taskArgs += @('-D', "$taskName=$($taskOptions[$taskName])")
    }
    & "$PSScriptRoot/idf.ps1" @taskArgs build
    $taskExit = $LASTEXITCODE
} finally { Pop-Location }
exit $taskExit
