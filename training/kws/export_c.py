"""Deterministic, untrained probe export. No audio or learned weights are implied."""
import hashlib
import json
import math
from pathlib import Path
import random

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'components/kws_c11/generated'
SEED = 20260921

def array(kind, name, values, static=True):
    prefix = 'static const' if static else 'const'
    rows = [','.join(str(x) for x in values[i:i+24]) for i in range(0,len(values),24)]
    return f'{prefix} {kind} {name}[{len(values)}]={{\n' + ',\n'.join(rows) + '\n};\n'

def make_probe():
    rng = random.Random(SEED)
    layers = []
    for i in range(12):
        dw = 0 < i < 11 and i % 2 == 1
        inputs, outputs = (40 if i == 0 else 24), (1 if i == 11 else 24)
        kernel = 3 if i == 0 else 5 if dw else 1
        n = outputs * kernel * (1 if dw else inputs)
        shifts = [rng.randrange(3,6) if dw else rng.randrange(4,8) for _ in range(outputs)]
        if i == 11: shifts = [-2]
        layers.append(dict(inputs=inputs,outputs=outputs,kernel=kernel,
            dilation=(1 << ((i-1)//2)) if dw else 1,depthwise=int(dw),
            relu=int(i < 11 and not dw),
            weights=[rng.randrange(-7,8) for _ in range(n)],
            bias=[0 if dw else rng.randrange(-96,97) for _ in range(outputs)],shift=shifts))
    return dict(name='xiaoyan_ds_tcn24_v1_probe',seed=SEED,trained=False,
                mean_q8=[3072]*40,inverse_std_q12=[1024]*40,layers=layers)

def main():
    OUT.mkdir(parents=True,exist_ok=True)
    probe=make_probe()
    parts=['#include "kws.h"\n']
    for i,l in enumerate(probe['layers']):
        parts.append(array('int8_t',f'w{i}',l['weights']))
        if not l['depthwise']: parts.append(array('int32_t',f'b{i}',l['bias']))
        parts.append(array('int8_t',f's{i}',l['shift']))
    parts.append(array('int16_t','mean',probe['mean_q8']))
    parts.append(array('uint16_t','inverse_std',probe['inverse_std_q12']))
    parts.append('static const kws_layer_t layers[12]={\n')
    for i,l in enumerate(probe['layers']):
        bias='NULL' if l['depthwise'] else f'b{i}'
        parts.append('{'+f'w{i},{bias},s{i},'+','.join(str(l[k]) for k in
            ('inputs','outputs','kernel','dilation','depthwise','relu'))+'},\n')
    parts.append('};\nconst kws_model_t kws_probe_model={"'+probe['name']+'",layers,mean,inverse_std,false};\n')
    (OUT/'probe.c').write_text(''.join(parts),encoding='utf-8',newline='\n')
    (OUT/'probe.json').write_text(json.dumps(probe,separators=(',',':'))+'\n',encoding='utf-8',newline='\n')
    hann=[math.floor(32767*(.5-.5*math.cos(2*math.pi*i/511))+.5) for i in range(512)]
    mel=lambda hz: 2595*math.log10(1+hz/700)
    hz=lambda m: 700*(10**(m/2595)-1)
    points=[hz(mel(125)+(mel(7500)-mel(125))*i/41) for i in range(42)]
    weights=[]; bands=[]
    for i in range(40):
        entries=[]
        for k in range(257):
            f=k*16000/512
            w=max(0,min((f-points[i])/(points[i+1]-points[i]),(points[i+2]-f)/(points[i+2]-points[i+1])))
            q=math.floor(w*32767+.5)
            if q: entries.append((k,q))
        assert entries and entries[-1][0]-entries[0][0]+1==len(entries)
        bands.append((entries[0][0],len(entries),len(weights)))
        weights.extend(q for _,q in entries)
    tables='#include "../kws_internal.h"\n'+array('int16_t','kws_hann',hann,False)
    tables+=array('uint16_t','kws_mel_weights',weights,False)
    tables+=array('uint16_t','kws_log_fraction',[math.floor(math.log2(1+i/256)*256+.5) for i in range(256)],False)
    tables+='const kws_mel_band_t kws_mel[40]={\n'+',\n'.join('{'+','.join(map(str,x))+'}' for x in bands)+'\n};\n'
    (OUT/'tables.c').write_text(tables,encoding='utf-8',newline='\n')
    twiddles=[(math.floor(32767*math.cos(-2*math.pi*i/512)+.5),math.floor(32767*math.sin(-2*math.pi*i/512)+.5)) for i in range(512)]
    fft='static const struct kiss_fft_state kws_fft_config={512,0,{4,128,4,32,4,8,4,2,2,1},{\n'
    fft+=',\n'.join('{'+str(r)+','+str(im)+'}' for r,im in twiddles)+'\n}};\n'
    (OUT/'fft_config.inc').write_text(fft,encoding='utf-8',newline='\n')
    counts=dict(weights=sum(len(l['weights']) for l in probe['layers']),
        folded_biases=sum(l['outputs'] for l in probe['layers'] if not l['depthwise']),
        train_parameters=6384+6*24*2+1,history_bytes=2*40+sum(4*d*24 for d in (1,2,4,8,16)),
        receptive_frames=1+2+sum(4*d for d in (1,2,4,8,16)),
        acoustic_receptive_ms=(1+2+sum(4*d for d in (1,2,4,8,16))-1)*16+32)
    counts['folded_weight_bias_bytes']=counts['weights']+4*counts['folded_biases']
    files={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in OUT.iterdir() if p.suffix in ('.c','.inc','.json') and p.name!='manifest.json'}
    (OUT/'manifest.json').write_text(json.dumps(dict(seed=SEED,trained=False,counts=counts,sha256=files),indent=2)+'\n',encoding='utf-8',newline='\n')
    print(json.dumps(counts))

if __name__=='__main__': main()
