param([string]$BuildDir = 'build', [switch]$Reconfigure, [switch]$SelfWake, [switch]$KeywordPcm, [switch]$Wide, [switch]$SyncConfirm, [switch]$Mega, [switch]$NoiseSuppress, [ValidateRange(500,950)][int]$Threshold=740)
$ErrorActionPreference = 'Stop'
$SelfWake = $SelfWake -or $Wide -or $SyncConfirm -or $Mega -or $NoiseSuppress
if ($KeywordPcm -and !$SelfWake) { throw 'KeywordPcm is a self-developed model diagnostic; use -SelfWake explicitly' }
if ($SyncConfirm -and !$Wide) { throw 'SyncConfirm requires Wide' }
if ($Mega -and !$SyncConfirm) { throw 'Mega requires SyncConfirm and Wide' }
if ($NoiseSuppress -and !$Mega) { throw 'NoiseSuppress is an explicit P4 Mega experiment' }
$p4Project = Split-Path -Parent $PSScriptRoot
$cogdProject = Split-Path -Parent $p4Project
$p4Options = @{
    AGENT_BOARD_P4='ON'; AGENT_APP_LTO='ON'; AGENT_KWS_C11=$(if ($SelfWake) {'ON'} else {'OFF'})
    AGENT_P4_WAKENET10=$(if ($SelfWake) {'OFF'} else {'ON'})
    AGENT_KWS_TRAINED=$(if ($SelfWake) {'ON'} else {'OFF'}); AGENT_KWS_CHANNELS=$(if ($Mega) {'96'} elseif ($SelfWake) {'48'} else {'24'}); AGENT_KWS_VERIFIED=$(if ($SelfWake) {'ON'} else {'OFF'})
    AGENT_KWS_VERIFIED_DIR=(Join-Path $p4Project $(if ($Mega) {'models/wake96'} elseif ($SyncConfirm) {'models/wake48_sync'} elseif ($Wide) {'models/wake48'} else {'models/wake4'}))
    AGENT_P4_KWS_WIDE=$(if ($Wide) {'ON'} else {'OFF'})
    AGENT_P4_KWS_SYNC_CONFIRM=$(if ($SyncConfirm) {'ON'} else {'OFF'})
    AGENT_P4_KWS_MEGA=$(if ($Mega) {'ON'} else {'OFF'})
    AGENT_P4_KWS_NS=$(if ($NoiseSuppress) {'ON'} else {'OFF'})
    AGENT_KWS_TEMPORAL_OWNER_SOURCE=(Join-Path $p4Project $(if ($SyncConfirm) {'board/sync_kws_temporal_owner.c'} else {'board/kws_temporal_owner.c'}))
    AGENT_KWS_FUSION='OFF'; AGENT_KWS_SILENCE_PRIME='ON'; AGENT_KWS_STABLE_WAKE='OFF'
    AGENT_TRAINED_WAKE_THRESHOLD="$Threshold"; AGENT_KEYWORD_VERIFY='OFF'; AGENT_BACKGROUND_VERIFY='ON'
    AGENT_PACKED_CLIP='ON'; AGENT_MIC_OVERSAMPLE='OFF'; AGENT_MIC_EXACT_CLOCK='OFF'
    AGENT_WS_INCREMENTAL='ON'; AGENT_ISOLATED_ASR='ON'; AGENT_TEXT_PREFETCH='ON'
    AGENT_LOCAL_FIRST_PREFETCH='ON'; AGENT_REQUEST_SCRATCH_COMPACT='ON'
    AGENT_CONTEXTUAL_CACHED_ACK='ON'; AGENT_VOICE_HTTP_RELEASE='ON'; AGENT_VOICE_HTTP_FORGET='ON'
    AGENT_VOICE_SCRATCH_LOANS='ON'; AGENT_USER_TEST='ON'; AGENT_CLOCK_BOOT='OFF'
    AGENT_KEYWORD_PCM=$(if ($KeywordPcm) {'ON'} else {'OFF'})
}
$p4Args = @('--no-ccache','-B',$BuildDir)
$p4Cache = Join-Path $p4Project "$BuildDir/CMakeCache.txt"
if (Test-Path -LiteralPath $p4Cache) {
    $p4OfficialEnabled = [bool](Select-String -LiteralPath $p4Cache -Pattern '^AGENT_P4_WAKENET10:[^=]+=ON$' -Quiet)
    if ($p4OfficialEnabled -ne (!$SelfWake)) { $Reconfigure = $true }
    $p4DiagnosticEnabled = [bool](Select-String -LiteralPath $p4Cache -Pattern '^AGENT_KEYWORD_PCM:[^=]+=ON$' -Quiet)
    if ($p4DiagnosticEnabled -ne [bool]$KeywordPcm) { $Reconfigure = $true }
    $p4WideEnabled = [bool](Select-String -LiteralPath $p4Cache -Pattern '^AGENT_P4_KWS_WIDE:[^=]+=ON$' -Quiet)
    if ($p4WideEnabled -ne [bool]$Wide) { $Reconfigure = $true }
    $p4SyncEnabled = [bool](Select-String -LiteralPath $p4Cache -Pattern '^AGENT_P4_KWS_SYNC_CONFIRM:[^=]+=ON$' -Quiet)
    if ($p4SyncEnabled -ne [bool]$SyncConfirm) { $Reconfigure = $true }
    $p4MegaEnabled = [bool](Select-String -LiteralPath $p4Cache -Pattern '^AGENT_P4_KWS_MEGA:[^=]+=ON$' -Quiet)
    if ($p4MegaEnabled -ne [bool]$Mega) { $Reconfigure = $true }
    $p4NsEnabled = [bool](Select-String -LiteralPath $p4Cache -Pattern '^AGENT_P4_KWS_NS:[^=]+=ON$' -Quiet)
    if ($p4NsEnabled -ne [bool]$NoiseSuppress) { $Reconfigure = $true }
    if (!(Select-String -LiteralPath $p4Cache -Pattern "^AGENT_TRAINED_WAKE_THRESHOLD:[^=]+=$Threshold`$" -Quiet)) { $Reconfigure = $true }
}
if ($Reconfigure -or $KeywordPcm -or !(Test-Path (Join-Path $p4Project "$BuildDir/CMakeCache.txt"))) {
    $p4Args += @('-D',"SDKCONFIG=$BuildDir/sdkconfig",'-D','SDKCONFIG_DEFAULTS=sdkconfig.defaults')
    foreach ($p4Name in ($p4Options.Keys | Sort-Object)) { $p4Args += @('-D',"$p4Name=$($p4Options[$p4Name])") }
}
Push-Location $p4Project
try { & (Join-Path $cogdProject 'tools/idf.ps1') @p4Args build; $p4Exit=$LASTEXITCODE }
finally { Pop-Location }
if ($p4Exit -eq 0) {
    & python (Join-Path $PSScriptRoot 'build_media_assets.py') (Join-Path $p4Project "$BuildDir/p4-assets.bin")
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}
exit $p4Exit
