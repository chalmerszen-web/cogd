"""Independently transcribe externally recorded replies after template alignment.

The played question contains no answer word. A recognized answer therefore
checks actual acoustic output, separately from serial TTS completion markers.
This remains automatic speech checking, not subjective sound-quality review.
"""
import argparse,hashlib,json,math
from pathlib import Path
import numpy as np
import soundfile as sf
from scipy.signal import correlate,find_peaks,resample_poly
import sherpa_onnx
ROOT=Path(__file__).resolve().parents[2]

def read16(path):
    samples,rate=sf.read(path,dtype='float32');assert samples.ndim==1
    divisor=math.gcd(rate,16000)
    return resample_poly(samples,16000//divisor,rate//divisor) if rate!=16000 else samples

def main():
    p=argparse.ArgumentParser();p.add_argument('--voice',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args()
    folder=a.voice.resolve();out=a.out.resolve();out.mkdir(parents=True,exist_ok=False)
    report=json.loads((folder/'report.json').read_text(encoding='utf8'));assert report['complete']
    assert '木星' not in report['prompt']
    recording=read16(folder/'speaker.wav');template=read16(folder/'prompt.wav');n=len(template)
    correlation=correlate(recording,template,mode='valid',method='fft')
    # Sliding energy without a long convolution.
    total=np.r_[0,np.cumsum(recording.astype(float)**2)];energy=total[n:]-total[:-n]
    normalized=correlation/np.sqrt(np.maximum(energy,1e-12)*float(np.sum(template.astype(float)**2)))
    peaks,_=find_peaks(normalized,height=.2,distance=n+32000)
    rounds=len(report['trials']);assert len(peaks)>=rounds,{'matched':len(peaks),'expected':rounds}
    selected=np.sort(peaks[np.argsort(normalized[peaks])[-rounds:]])
    # Alignment must also preserve the recorded monotonic spacing of inputs.
    expected=np.array([t['attempts'][0]['prompt_playback']['source_started'] for t in report['trials']])
    actual=selected/16000
    assert np.max(np.abs((actual-actual[0])-(expected-expected[0])))<.5
    model=ROOT/'artifacts/kws-phase2/asr'
    decoder=sherpa_onnx.OfflineRecognizer.from_sense_voice(model=str(model/'model.int8.onnx'),tokens=str(model/'tokens.txt'),num_threads=4,use_itn=True,language='zh')
    rows=[]
    for i,position in enumerate(selected):
        start=int(position+n);end=int(selected[i+1]-16000) if i+1<len(selected) else len(recording)
        assert 16000<=end-start<=60*16000
        clip=recording[start:end];stream=decoder.create_stream();stream.accept_waveform(16000,np.r_[clip,np.zeros(8000,dtype=np.float32)]);decoder.decode_stream(stream)
        path=out/f'reply-{i+1:02d}.wav';sf.write(path,clip,16000,subtype='PCM_16')
        rows.append({'round':i+1,'crop_seconds':[start/16000,end/16000],'alignment_correlation':float(normalized[position]),'transcript':stream.result.text,'contains_answer':'木星' in stream.result.text,'wav_sha256':hashlib.sha256(path.read_bytes()).hexdigest()})
    result={'complete':all(r['contains_answer'] for r in rows),'source_sha256':hashlib.sha256((folder/'speaker.wav').read_bytes()).hexdigest(),
            'script_sha256':hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
            'asr_model_sha256':hashlib.sha256((model/'model.int8.onnx').read_bytes()).hexdigest(),
            'method':'External microphone; repeated question template correlation; local SenseVoice zh with no target transcript prompt',
            'scope':'Automatic acoustic output verification; no subjective audio quality claim','replies':rows}
    (out/'report.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf8')
    print(json.dumps(result,ensure_ascii=False));assert result['complete']

if __name__=='__main__':main()
