param([ValidateSet('probe','trained','legacy','noaudio')][string]$Mode='probe',
      [ValidateRange(500,950)][int]$WakeThreshold=920,[switch]$Fusion,
      [ValidateSet('ef','ek','el')][string]$FusionPair='ek',[switch]$StableWake)
$ErrorActionPreference='Stop'
if($Fusion -and $Mode -ne 'trained') {throw 'Fusion requires trained mode'}
if($StableWake -and $Mode -ne 'trained') {throw 'StableWake requires trained mode'}
$taskBuild="build-kws-$Mode"
if($PSBoundParameters.ContainsKey('FusionPair') -and -not $Fusion) {throw 'FusionPair requires Fusion'}
if($Fusion -and $FusionPair -ne 'ef') {
    $taskBuild="build-kws-fusion-$FusionPair"
}
$taskAudio=if($Mode -eq 'noaudio') {'OFF'} else {'ON'}
$taskKws=if($Mode -eq 'legacy') {'OFF'} else {'ON'}
$taskKeyword=if($Mode -eq 'legacy') {'ON'} else {'OFF'}
$taskTrained=if($Mode -eq 'trained') {'ON'} else {'OFF'}
$taskDefaults='sdkconfig.defaults;sdkconfig.upgrade.defaults'
if($Mode -eq 'noaudio') {$taskDefaults+=';sdkconfig.noaudio.defaults'}
Push-Location (Split-Path -Parent $PSScriptRoot)
try {
    & "$PSScriptRoot/idf.ps1" --no-ccache -B $taskBuild -D "SDKCONFIG=$taskBuild/sdkconfig" `
        -D "SDKCONFIG_DEFAULTS=$taskDefaults" -D "AGENT_KWS_C11=$taskKws" `
        -D "AGENT_KWS_TRAINED=$taskTrained" `
        -D "AGENT_KWS_STABLE_WAKE=$($StableWake.IsPresent)" `
        -D "AGENT_KWS_FUSION=$($Fusion.IsPresent)" `
        -D "AGENT_KWS_FUSION_PAIR=$FusionPair" `
        -D "AGENT_TRAINED_WAKE_THRESHOLD=$WakeThreshold" `
        -D "AGENT_KEYWORD_VERIFY=$taskKeyword" -D "AGENT_MIC_OVERSAMPLE=$taskAudio" `
        -D AGENT_MIC_ATTEN_DB=12 -D "AGENT_BACKGROUND_VERIFY=$taskAudio" `
        -D "AGENT_PACKED_CLIP=$taskAudio" build
    $taskExit=$LASTEXITCODE
} finally {Pop-Location}
exit $taskExit
