param([string]$Out = 'artifacts/kws-bilingual/sapi-probe')
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Speech
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$destination = [IO.Path]::GetFullPath((Join-Path $root $Out))
if (-not $destination.StartsWith($root + [IO.Path]::DirectorySeparatorChar)) { throw 'Output must be inside workspace' }
if (Test-Path -LiteralPath $destination) { throw 'Preserve existing evidence; choose a new output directory' }
New-Item -ItemType Directory -Path $destination | Out-Null
$voices = @('Microsoft Huihui', 'Microsoft Kangkang', 'Microsoft Yaoyao')
$negative = @('你好', '小言', '你好小燕', '你好小杨', '你好小王', '你好小智', '嗨乐鑫', '请把灯打开', '今天天气很好')
$manifest = [System.Collections.Generic.List[object]]::new()
$synth = New-Object System.Speech.Synthesis.SpeechSynthesizer
try {
    $format = New-Object System.Speech.AudioFormat.SpeechAudioFormatInfo(16000, [System.Speech.AudioFormat.AudioBitsPerSample]::Sixteen, [System.Speech.AudioFormat.AudioChannel]::Mono)
    foreach ($voice in $voices) {
        $synth.SelectVoice($voice)
        $index = 0
        $requests = [System.Collections.Generic.List[object]]::new()
        foreach ($rate in @(-1, 0, 1)) {
            foreach ($phrase in @('你好，小言', '你好小言')) {
                $requests.Add(@{text=$phrase; rate=$rate; label=1})
            }
        }
        foreach ($phrase in $negative) { $requests.Add(@{text=$phrase; rate=0; label=0}) }
        foreach ($request in $requests) {
            $name = ('sapi-' + $voice.Split(' ')[1].ToLowerInvariant() + '-' + $index.ToString('D2'))
            $path = Join-Path $destination ($name + '.wav')
            $synth.Rate = $request.rate
            $synth.Volume = 100
            $synth.SetOutputToWaveFile($path, $format)
            $synth.Speak($request.text)
            $synth.SetOutputToNull()
            $manifest.Add(@{clip_id=$name; path=$path; text=$request.text; rate=$request.rate;
                label=$request.label; language='zh'; source_group=$voice; source_kind='Microsoft SAPI local synthetic';
                split='diagnosis'; wav_sha256=(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()})
            $index++
        }
    }
} finally { $synth.Dispose() }
$utf8 = New-Object Text.UTF8Encoding($false)
[IO.File]::WriteAllText((Join-Path $destination 'manifest.json'), (ConvertTo-Json -InputObject $manifest.ToArray() -Depth 5), $utf8)
[IO.File]::WriteAllText((Join-Path $destination 'scope.txt'), 'Independent TTS-engine diagnosis only. No human generalization claim. This material is not approved for training or redistribution by this script. No audio output device is opened.', $utf8)
Write-Output ('Generated ' + $manifest.Count + ' local WAVs without speaker playback.')
