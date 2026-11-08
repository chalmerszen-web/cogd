param([string]$BuildDir = 'build', [switch]$Reconfigure, [ValidateSet('WSS','AOQ')][string]$CommunicationMode='WSS', [switch]$AoqProbe, [switch]$SelfWake, [switch]$KeywordPcm, [switch]$Wide, [switch]$SyncConfirm, [switch]$Mega, [switch]$NoiseSuppress, [ValidateRange(500,950)][int]$Threshold=740)
$ErrorActionPreference = 'Stop'
$SelfWake = $SelfWake -or $Wide -or $SyncConfirm -or $Mega -or $NoiseSuppress
if ($KeywordPcm -and !$SelfWake) { throw 'KeywordPcm is a self-developed model diagnostic; use -SelfWake explicitly' }
if ($SyncConfirm -and !$Wide) { throw 'SyncConfirm requires Wide' }
if ($Mega -and !$SyncConfirm) { throw 'Mega requires SyncConfirm and Wide' }
if ($NoiseSuppress -and !$Mega) { throw 'NoiseSuppress is an explicit P4 Mega experiment' }
$p4Project = Split-Path -Parent $PSScriptRoot
$cogdProject = Split-Path -Parent $p4Project
$p4Options = @{
    COGD_COMM_MODE=$CommunicationMode
    AGENT_P4_AOQ_PROBE=$(if ($AoqProbe) {'ON'} else {'OFF'})
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
# Defaults alone do not update an existing sdkconfig. Apply these transport
# settings on mode switches as well, so a reused AOQ directory cannot silently
# retain its settings in a subsequent WSS build (or vice versa).
$p4Config = Join-Path $p4Project "$BuildDir/sdkconfig"
$p4TransportConfig = @{
    ESP_HOSTED_SDIO_RX_DOUBLE_BUFFERING=$(if ($CommunicationMode -eq 'AOQ') {'n'} else {'y'})
    LWIP_UDP_RECVMBOX_SIZE=$(if ($CommunicationMode -eq 'AOQ') {'64'} else {'32'})
    LWIP_TCPIP_RECVMBOX_SIZE=$(if ($CommunicationMode -eq 'AOQ') {'64'} else {'32'})
}
if (Test-Path -LiteralPath $p4Config) {
    $p4ConfigText = [IO.File]::ReadAllText($p4Config)
    $p4OriginalConfigText=$p4ConfigText
    foreach ($p4Key in $p4TransportConfig.Keys) {
        $p4Value=$p4TransportConfig[$p4Key]
        $p4Line=if ($p4Value -eq 'n') {"# CONFIG_$p4Key is not set"} else {"CONFIG_$p4Key=$p4Value"}
        $p4Pattern="(?m)^(?:CONFIG_$p4Key=.*|# CONFIG_$p4Key is not set)\r?$"
        $p4Updated=[regex]::Replace($p4ConfigText,$p4Pattern,$p4Line)
        if ($p4Updated -eq $p4ConfigText -and $p4ConfigText -notmatch $p4Pattern) { $p4Updated += "`n$p4Line`n" }
        if ($p4Updated -ne $p4ConfigText) { $p4ConfigText=$p4Updated;$Reconfigure=$true }
    }
    if ($p4ConfigText -ne $p4OriginalConfigText) { [IO.File]::WriteAllText($p4Config,$p4ConfigText) }
}
$p4Cache = Join-Path $p4Project "$BuildDir/CMakeCache.txt"
if (Test-Path -LiteralPath $p4Cache) {
    if (!(Select-String -LiteralPath $p4Cache -Pattern "^COGD_COMM_MODE:[^=]+=$CommunicationMode`$" -Quiet)) { $Reconfigure = $true }
    $p4AoqEnabled = [bool](Select-String -LiteralPath $p4Cache -Pattern '^AGENT_P4_AOQ_PROBE:[^=]+=ON$' -Quiet)
    if ($p4AoqEnabled -ne [bool]$AoqProbe) { $Reconfigure = $true }
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
    # Verify the compiler's generated configuration, not just requested
    # sdkconfig/defaults text. Kconfig can reject a value and still exit zero.
    $p4GeneratedHeader = Join-Path $p4Project "$BuildDir/config/sdkconfig.h"
    $p4Generated = [IO.File]::ReadAllText($p4GeneratedHeader)
    foreach ($p4Key in $p4TransportConfig.Keys) {
        $p4Requested = $p4TransportConfig[$p4Key]
        $p4Define = "(?m)^#define CONFIG_$p4Key\s+(\S+)\r?$"
        $p4Match = [regex]::Match($p4Generated,$p4Define)
        $p4Actual = if ($p4Match.Success) { $p4Match.Groups[1].Value } else { 'n' }
        $p4Expected = if ($p4Requested -eq 'y') { '1' } else { $p4Requested }
        if ($p4Actual -ne $p4Expected) {
            throw "Generated $CommunicationMode configuration mismatch: CONFIG_$p4Key requested=$p4Requested actual=$p4Actual"
        }
    }
    Write-Output "Generated $CommunicationMode transport configuration verified"
    & python (Join-Path $PSScriptRoot 'build_media_assets.py') (Join-Path $p4Project "$BuildDir/p4-assets.bin")
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}
exit $p4Exit
