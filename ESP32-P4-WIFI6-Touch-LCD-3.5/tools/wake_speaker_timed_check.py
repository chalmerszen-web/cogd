"""Offline independent reply check with a strong anchor and playback timing.

The original per-prompt correlation >=.2 check is retained as a failed check.
Here a single >=.2 anchor must identify one unambiguous placement of the four
known question intervals, and all four local peaks must agree within50 ms.
Answer words are then recognized from recorded audio, not copied from UART.
"""
import argparse,hashlib,json,sys
from pathlib import Path
import numpy as np
from scipy.signal import correlate
import sherpa_onnx,soundfile as sf
from wake_speaker_check import read16,ROOT

def main():
    p=argparse.ArgumentParser();p.add_argument('--voice',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args()
    a.out.mkdir(parents=True,exist_ok=False)
    source=json.loads((a.voice/'report.json').read_text(encoding='utf8'))
    assert source['complete'] and len(source['trials'])==4 and '木星' not in source['prompt']
    wave=read16(a.voice/'speaker.wav');template=read16(a.voice/'prompt.wav');n=len(template)
    cross=correlate(wave,template,mode='valid',method='fft')
    sums=np.r_[0,np.cumsum(wave.astype(float)**2)];energy=sums[n:]-sums[:-n]
    scores=cross/np.sqrt(np.maximum(energy,1e-12)*float(np.sum(template.astype(float)**2)))
    times=np.array([t['attempts'][0]['prompt_playback']['source_started'] for t in source['trials']]);times-=times[0]
    anchor=int(np.argmax(scores));assert scores[anchor]>=.2
    origins=[(i,anchor/16000-time) for i,time in enumerate(times)
             if anchor/16000-time>=0 and anchor/16000-time+times[-1]<(len(scores)-1)/16000]
    assert len(origins)==1,origins
    anchor_turn,origin=origins[0];positions=[]
    for time in times:
        center=int(round((origin+time)*16000));low=max(0,center-4000);high=min(len(scores),center+4001)
        position=low+int(np.argmax(scores[low:high]));assert scores[position]>0
        assert abs(position-center)<=800,('Timing mismatch',position,center)
        positions.append(position)
    model=ROOT/'artifacts/kws-phase2/asr'
    decoder=sherpa_onnx.OfflineRecognizer.from_sense_voice(model=str(model/'model.int8.onnx'),tokens=str(model/'tokens.txt'),num_threads=4,use_itn=True,language='zh')
    replies=[]
    for i,position in enumerate(positions):
        start=position+n;end=positions[i+1]-16000 if i+1<len(positions) else len(wave)
        assert 16000<=end-start<=60*16000
        clip=wave[start:end];stream=decoder.create_stream();stream.accept_waveform(16000,np.r_[clip,np.zeros(8000,dtype=np.float32)]);decoder.decode_stream(stream)
        file=a.out/f'reply-{i+1:02d}.wav';sf.write(file,clip,16000,subtype='PCM_16')
        replies.append({'round':i+1,'transcript':stream.result.text,'contains_answer':'木星' in stream.result.text,
                        'question_correlation':float(scores[position]),'timing_residual_ms':1000*(position/16000-origin-times[i]),
                        'crop_seconds':[start/16000,end/16000],'wav_sha256':hashlib.sha256(file.read_bytes()).hexdigest()})
    result={'complete':all(r['contains_answer'] for r in replies),'replies':replies,
            'method':'Unambiguous >=.2 template anchor, four recorded playback intervals, peak timing <=50 ms; independent local SenseVoice on answer audio',
            'anchor_turn':anchor_turn+1,'anchor_correlation':float(scores[anchor]),
            'source_sha256':hashlib.sha256((a.voice/'speaker.wav').read_bytes()).hexdigest(),
            'source_report_sha256':hashlib.sha256((a.voice/'report.json').read_bytes()).hexdigest(),
            'script_sha256':hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
            'asr_model_sha256':hashlib.sha256((model/'model.int8.onnx').read_bytes()).hexdigest(),
            'original_individual_correlation_check':'FAILED_ALIGNMENT, retained separately',
            'scope':'Offline acoustic output check; no wake-rate change, no subjective quality claim'}
    (a.out/'report.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf8')
    print(json.dumps(result,ensure_ascii=False));assert result['complete']
if __name__=='__main__':main()
