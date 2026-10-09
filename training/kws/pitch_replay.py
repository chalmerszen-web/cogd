"""Reconstruct existing TRAIN PCM recipes and append exact C pitch features.

All existing raw40/y/metadata are checked, not regenerated for training. This
does not touch DEV/TEST waveforms, select sources by results, or train models.
The recipes below mirror the frozen data/device/channel/paired/compact stages.
"""
from collections import Counter
import hashlib
import json
from pathlib import Path
import time
import numpy as np

from data import ROOT, framed_example, load_pcm, normalize, read_manifest
from append_captures import capture_path, respeed
from device_domain import pcm
from channel_augment import spectrum
from paired_negative import incomplete as time_cut
from energy_cut import incomplete as energy_cut
from prepare_weak_logmel import mix_weak
from pitch_batch import BatchFrontend


def read(path):
    return json.loads(Path(path).read_text(encoding='utf8'))


class Reader:
    """Only the preregistered TRAIN waveform files may be decoded."""
    def __init__(self,allowed,forbidden):
        self.allowed={str((ROOT/path).resolve()) for path in allowed if path.endswith('.wav')}
        self.forbidden=forbidden
        self.cache={}

    def recording(self,path,expected=None):
        path=capture_path(path).resolve()
        assert str(path) in self.allowed,str(path)
        if str(path) not in self.cache:self.cache[str(path)]=pcm(path)
        signal=self.cache[str(path)]
        if expected is not None:assert hashlib.sha256(signal.astype('<i2').tobytes()).hexdigest()==expected
        return signal

    def source(self,row):
        assert row['split']=='train' and row['source_group'] not in self.forbidden
        signal=self.recording(row['path'],row['pcm_sha256'])
        if row['label']:assert row['wake_end_sample']<=len(signal)
        return signal


class Recorder:
    def __init__(self,train,stats,library,out,maximum_seconds):
        self.began=time.monotonic();self.maximum_seconds=maximum_seconds
        self.out=Path(out);self.out.mkdir(exist_ok=False)
        with np.load(train,allow_pickle=False) as source:
            self.old={k:source[k] for k in source.files}
        self.stats=read(stats);self.frontend=BatchFrontend(library)
        n=len(self.old['label'])
        assert n==15791 and self.old['x'].shape==(n,256,40)
        keys=list(zip(self.old['clip_id'].tolist(),self.old['variant'].tolist()))
        assert len(set(keys))==n,'TRAIN clip/variant keys must be unique'
        self.indices={key:i for i,key in enumerate(keys)}
        self.filled=np.zeros(n,dtype=bool)
        self.features=np.zeros((n,256,2),dtype=np.int16)
        self.audit=[];self.counts=Counter();self.values=0

    def add(self,family,row,variant,signal,labels,end,device=False):
        if time.monotonic()-self.began>self.maximum_seconds:raise TimeoutError('Finite PCM replay budget exceeded')
        key=(row['clip_id'],variant)
        assert key in self.indices,('Missing originalTRAIN row',key)
        index=self.indices[key]
        assert not self.filled[index],('RepeatedTRAIN row',key)
        for field in ('label','language','source_group'):
            assert self.old[field][index]==row[field],(field,key)
        assert bool(self.old['device_domain'][index])==device,key
        np.testing.assert_array_equal(labels,self.old['y'][index])
        assert self.old['event_end'][index]==(-1 if end is None else end),key
        logmel,pitch=self.frontend(signal)
        inputs=normalize(logmel,self.stats)
        if not np.array_equal(inputs,self.old['x'][index]):
            difference=np.argwhere(inputs!=self.old['x'][index])
            failure=dict(family=family,key=list(key),index=index,values=int(len(difference)),
                first=difference[:8].tolist(),old_PCM_reconstruction_failed=True)
            (self.out/'input-mismatch.json').write_text(json.dumps(failure,indent=2)+'\n',encoding='utf8')
            raise AssertionError('PCM reconstruction mismatch; no replacement or skipped row: '+str(key))
        assert pitch.shape==(256,2)
        self.features[index]=pitch;self.filled[index]=True
        self.values+=inputs.size;self.counts[family]+=1
        self.audit.append(dict(index=index,clip_id=row['clip_id'],variant=variant,family=family,
            PCM_sha256=hashlib.sha256(signal.astype('<i2').tobytes()).hexdigest()))

    def checkpoint(self,family):
        report=dict(family=family,rows=int(self.filled.sum()),total=len(self.filled),counts=dict(self.counts),
            seconds=time.monotonic()-self.began)
        (self.out/'progress.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf8')
        print(json.dumps(report),flush=True)

    def finish(self):
        missing=np.flatnonzero(~self.filled)
        if len(missing):
            (self.out/'missing.json').write_text(json.dumps([dict(index=int(i),clip_id=str(self.old['clip_id'][i]),variant=int(self.old['variant'][i])) for i in missing],indent=2)+'\n',encoding='utf8')
            raise AssertionError(f'{len(missing)} originalTRAIN rows not reconstructed')
        np.save(self.out/'pitch.npy',self.features)
        np.testing.assert_array_equal(np.load(self.out/'pitch.npy'),self.features)
        result=dict(complete=True,rows=len(self.filled),old_C_input_values_exact=self.values,
            pitch_values=int(self.features.size),counts=dict(self.counts),seconds=time.monotonic()-self.began,
            all_labels_metadata_original_inputs_preserved=True,no_missing_or_skipped_rows=True,
            no_DEV_TEST_audio=True,no_fit_or_quality_claim=True)
        (self.out/'row-provenance.json').write_text(json.dumps(self.audit,separators=(',',':'))+'\n',encoding='utf8')
        (self.out/'audit.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf8')
        return result


def phase3(rec,reader,rows,metadata):
    rng=np.random.default_rng(metadata['seed'])
    noise=[r for r in rows if r['split']=='train' and not r['label'] and
        (r.get('source_dataset')=='google/fleurs' or r.get('source_kind'))]
    for row in rows:
        if row['split']!='train':continue
        raw=reader.source(row)
        for variant in range(metadata['augmentations']+1):
            background=None
            if variant and noise and rng.random()<.5:
                background=reader.source(noise[int(rng.integers(len(noise)))])
            wave,y,end=framed_example(row,raw,rng if variant else None,background=background)
            rec.add('phase3',row,variant,wave,y,end)
    rec.checkpoint('phase3')


def device_sources(reader,parents,metadata,adjust_start=False):
    result=[]
    for cap in metadata['captures']:
        if cap['split']!='train':continue
        parent=parents[cap['parent_clip_id']];recorded=reader.recording(cap['recording'],cap['pcm_sha256'])
        lo,hi=cap['crop'];low,high=cap['end_interval']
        row=dict(parent,clip_id=cap['clip_id'],endpoint_schema=1,speech_end_low_sample=low,
            speech_end_high_sample=high,sampling_group='positive' if parent['label'] else 'hard_negative')
        if adjust_start:row['speech_start_sample']=max(0,cap['lag_samples']-lo+parent['speech_start_sample'])
        result.append((row,recorded[lo:hi],recorded[-16000:]))
    assert len(result)==44
    return result


def device(rec,reader,parents,metadata):
    sources=device_sources(reader,parents,metadata);noise=[r[2] for r in sources]
    rng=np.random.default_rng(metadata['seed'])
    for row,raw,ambient in sources:
        for variant in range(9):
            background=noise[int(rng.integers(len(noise)))]
            wave,y,end=framed_example(row,raw,rng if variant else None,background=background)
            if not variant:
                offset=(max(4096,33792-row['speech_end_low_sample'])+len(wave)-len(raw)-1024)//2
                floor=np.resize(background,len(wave));wave[:offset]=floor[:offset];wave[offset+len(raw):]=floor[offset+len(raw):]
            rec.add('device',row,variant,wave,y,end,True)
        quiet=dict(row,clip_id=row['clip_id']+'-ambient',label=0)
        rec.add('device',quiet,0,np.resize(ambient,65536).astype(np.int16),np.zeros(256,dtype=np.float32),None,True)
    rec.checkpoint('device')


def channel_parameters(reader,device_meta,channel_meta):
    trials={}
    for report in device_meta['source_reports']:
        for trial in read(capture_path(report))['trials']:trials[trial['source']['clip_id']]=trial
    frequencies=np.fft.rfftfreq(512,1/16000);edges=np.geomspace(125,7600,21)
    centres=np.sqrt(edges[:-1]*edges[1:]);curves=[];noise=[]
    for cap in device_meta['captures']:
        if cap['split']!='train':continue
        trial=trials[cap['parent_clip_id']];row=trial['source']
        recorded=reader.recording(trial['path']).astype(float)
        leveled=capture_path(trial['path']).parent/'leveled-sources'/(row['clip_id']+'.wav')
        source=reader.recording(leveled).astype(float)*row['playback_gain']
        lag=cap['lag_samples'];actual=recorded[lag:lag+len(source)]
        clean,observed,floor=spectrum(source),spectrum(actual),spectrum(recorded[-16000:])
        curve=[]
        for low,high in zip(edges[:-1],edges[1:]):
            mask=(frequencies>=low)&(frequencies<high)
            power=max(1.,float((observed[mask]-floor[mask]).sum()))
            curve.append(np.clip(10*np.log10(power/max(1.,clean[mask].sum())),-18,18))
        curves.append(curve);noise.append(recorded[-16000:].astype(np.int16))
    median=np.median(curves,axis=0)
    np.testing.assert_array_equal(median,np.array(channel_meta['median_response_db']))
    np.testing.assert_array_equal(centres,np.array(channel_meta['centre_frequencies']))
    return frequencies,centres,curves,median,noise


def channel(rec,reader,rows,parameters,metadata):
    frequencies,centres,curves,median,noise=parameters
    rng=np.random.default_rng(metadata['seed'])
    for row in rows:
        if row['split']!='train':continue
        source=reader.source(row)
        for variant in range(2):
            wave,y,end=framed_example(row,source,rng)
            response=.5*median+.5*np.asarray(curves[int(rng.integers(len(curves)))])
            response+=rng.normal(0,2,len(response))
            impulse=np.roll(np.fft.irfft(10**(np.interp(frequencies,centres,response)/20),n=512),256)*np.hanning(512)
            colored=np.convolve(wave.astype(float),impulse,mode='same')
            ambient=np.resize(noise[int(rng.integers(len(noise)))],len(wave)).astype(float)
            ambient*=10**(rng.uniform(-3,3)/20)
            active=np.abs(colored)>max(1.,np.max(np.abs(colored))*.08)
            rms=np.sqrt(np.mean(colored[active]**2)) if np.any(active) else 1.
            colored*=max(1.,np.sqrt(np.mean(ambient**2)))*10**(rng.uniform(0,22)/20)/max(1.,rms)
            signal=np.clip(np.rint(colored+ambient),-32768,32767).astype(np.int16)
            rec.add('channel',dict(row,clip_id='channel-'+row['clip_id']),variant,signal,y,end)
    rec.checkpoint('channel')


def paired(rec,reader,rows,parents,device_meta,channel_meta,parameters,metadata):
    frequencies,centres,_,median,noise=parameters
    impulse=np.roll(np.fft.irfft(10**(np.interp(frequencies,centres,median)/20),n=512),256)*np.hanning(512)
    sources=[(row,reader.source(row),False,None) for row in rows if row['split']=='train' and row['label']]
    sources.extend((row,raw,True,ambient) for row,raw,ambient in device_sources(reader,parents,device_meta,True) if row['label'])
    rng=np.random.default_rng(metadata['seed'])
    for parent,raw,captured,ambient in sources:
        for side in ('prefix','suffix'):
            for variant in range(2):
                fraction=.5 if not variant else float(rng.uniform(.4,.6))
                part,_=time_cut(raw,parent['speech_start_sample'],parent['speech_end_low_sample'],side,fraction,ambient)
                row=dict(parent,label=0,clip_id=parent['clip_id']+'-'+side)
                wave,y,end=framed_example(row,part,rng if variant else None)
                if captured and not variant:
                    offset=(max(4096,33792-row['speech_end_low_sample'])+len(wave)-len(part)-1024)//2
                    floor=np.resize(ambient,len(wave));wave[:offset]=floor[:offset];wave[offset+len(part):]=floor[offset+len(part):]
                elif not captured and variant:
                    colored=np.convolve(wave.astype(float),impulse,mode='same')
                    floor=np.resize(noise[int(rng.integers(len(noise)))],len(wave)).astype(float)
                    active=np.abs(colored)>max(1.,np.max(np.abs(colored))*.08)
                    rms=np.sqrt(np.mean(colored[active]**2)) if np.any(active) else 1.
                    colored*=max(1.,np.sqrt(np.mean(floor**2)))*10**(rng.uniform(0,22)/20)/max(1.,rms)
                    wave=np.clip(np.rint(colored+floor),-32768,32767).astype(np.int16)
                rec.add('paired',row,variant,wave,y,end,captured)
    rec.checkpoint('paired')


def append_sources(reader,parents,audit,report):
    trials={t['source']['clip_id']:t for t in report['trials']};result=[]
    for accepted in audit['trials']:
        if not accepted['eligible']:continue
        parent=parents[accepted['parent_clip_id']];assert parent['split']=='train'
        trial=trials[accepted['clip_id']];raw=reader.source(parent)
        recorded=reader.recording(trial['path'],trial['signal']['sha256'])
        lag=accepted['lag_samples'];lo,hi=max(0,lag-1600),min(len(recorded),lag+len(raw)+1600)
        row=dict(parent,clip_id='device-extra-'+accepted['clip_id'],speech_start_sample=lag-lo+parent['speech_start_sample'],
            speech_end_low_sample=lag-lo+parent['speech_end_low_sample']-320,
            speech_end_high_sample=lag-lo+parent['speech_end_high_sample']+320)
        result.append((row,recorded[lo:hi],recorded[-16000:]))
    return result


def appended(rec,reader,parents,descriptor):
    meta,audit,report=[read(ROOT/descriptor[key]) for key in ('metadata','audit','report')]
    rng=np.random.default_rng(meta['seed'])
    sources=append_sources(reader,parents,audit,report)
    assert len(sources)==meta['captures']
    for row,raw,noise in sources:
        variants=[(row,raw)]
        if row['label']:
            for side in ('prefix','suffix'):
                part,_=time_cut(raw,row['speech_start_sample'],row['speech_end_low_sample'],side,ambient=noise)
                variants.append((dict(row,clip_id=row['clip_id']+'-'+side,label=0),part))
        for moved,part in variants:
            for variant,speed in enumerate((1.,.85,1.15)):
                try:changed,scaled=respeed(part,moved,speed)
                except ValueError:continue  # Same fixed speed fit rejection as original stage.
                wave,y,end=framed_example(scaled,changed)
                offset=(max(4096,33792-scaled['speech_end_low_sample'])+len(wave)-len(changed)-1024)//2
                floor=np.resize(noise,len(wave));wave[:offset]=floor[:offset];wave[offset+len(changed):]=floor[offset+len(changed):]
                if variant:wave=np.clip(np.rint(wave.astype(float)*10**(rng.uniform(-3,3)/20)),-32768,32767).astype(np.int16)
                rec.add(descriptor['family'],scaled,variant,wave,y,end,True)
        rec.add(descriptor['family'],dict(row,clip_id=row['clip_id']+'-ambient',label=0),0,
            np.resize(noise,65536).astype(np.int16),np.zeros(256,dtype=np.float32),None,True)
    rec.checkpoint(descriptor['family'])
    return sources


def weak(rec,reader,parents,device_meta,append_descriptors,metadata):
    measured=device_sources(reader,parents,device_meta,True)
    for descriptor in append_descriptors[:2]:
        measured.extend(append_sources(reader,parents,read(ROOT/descriptor['audit']),read(ROOT/descriptor['report'])))
    assert len(measured)==113
    rng=np.random.default_rng(metadata['seed'])
    for index,(row,source,noise) in enumerate(measured):
        variants=[(row,source)]
        if row['label']:
            for side in ('prefix','suffix'):
                part,_=time_cut(source,row['speech_start_sample'],row['speech_end_low_sample'],side,ambient=noise)
                variants.append((dict(row,label=0,clip_id=row['clip_id']+'-'+side),part))
        for moved,part in variants:
            wave,y,end=framed_example(moved,part)
            offset=(max(4096,33792-moved['speech_end_low_sample'])+len(wave)-len(part)-1024)//2
            floor=np.resize(noise,len(wave));wave[:offset]=floor[:offset];wave[offset+len(part):]=floor[offset+len(part):]
            for db in (6,12):
                other=int(rng.choice([i for i in range(len(measured)) if i!=index]))
                background=np.roll(measured[other][2],int(rng.integers(16000)))
                combined,_=mix_weak(wave,background,db)
                rec.add('weak',dict(moved,clip_id='weak-logmel-'+moved['clip_id']),db,combined,y,end,True)
    rec.checkpoint('weak')


def compact(rec,reader,metadata,raw_rows,noise):
    rng=np.random.default_rng(metadata['seed'])
    for source in metadata['sources']:
        if source['split']!='train':continue
        row=raw_rows[source['clip_id'].removeprefix('compact-')]
        raw=reader.source(row)
        bounds={key:source[key] for key in ('speech_start_sample','speech_end_low_sample','speech_end_high_sample')}
        moved=dict(row,**bounds,endpoint_schema=1,clip_id=source['clip_id'])
        variants=[(moved,raw)]
        for side in ('prefix','suffix'):
            part,_=time_cut(raw,bounds['speech_start_sample'],bounds['speech_end_low_sample'],side)
            a,b=bounds['speech_start_sample'],bounds['speech_end_low_sample']
            retained=float(np.sum(part[a:b].astype(np.float64)**2)/np.sum(raw[a:b].astype(np.float64)**2))
            if retained>.90:part,_=energy_cut(raw,a,b,side)
            variants.append((dict(moved,clip_id=moved['clip_id']+'-'+side,label=0),part))
        for variant_row,signal in variants:
            for variant in range(9):
                background=noise[int(rng.integers(len(noise)))] if variant else None
                wave,y,end=framed_example(variant_row,signal,rng if variant else None,background=background)
                rec.add('compact',variant_row,variant,wave,y,end)
    rec.checkpoint('compact')


def prepare(plan,out):
    paths=plan['files'];rows=read_manifest(ROOT/paths['manifest']);parents={r['clip_id']:r for r in rows}
    forbidden={r['source_group'] for r in rows if r['split']!='train'}
    reader=Reader(plan['hashes'],forbidden)
    rec=Recorder(ROOT/paths['train'],ROOT/paths['normalization'],ROOT/paths['batch_library'],out,plan['maximum_seconds'])
    device_meta,channel_meta,paired_meta,weak_meta,compact_meta=[read(ROOT/paths[key]) for key in ('device_metadata','channel_metadata','paired_metadata','weak_metadata','compact_metadata')]
    phase3(rec,reader,rows,read(ROOT/paths['phase3_metadata']))
    device(rec,reader,parents,device_meta)
    parameters=channel_parameters(reader,device_meta,channel_meta)
    channel(rec,reader,rows,parameters,channel_meta)
    paired(rec,reader,rows,parents,device_meta,channel_meta,parameters,paired_meta)
    for descriptor in plan['appended'][:2]:appended(rec,reader,parents,descriptor)
    weak(rec,reader,parents,device_meta,plan['appended'],weak_meta)
    raw_rows={r['clip_id']:r for r in (json.loads(line) for line in (ROOT/paths['compact_manifest']).read_text(encoding='utf8').splitlines())}
    compact(rec,reader,compact_meta,raw_rows,parameters[4])
    for descriptor in plan['appended'][2:]:appended(rec,reader,parents,descriptor)
    return rec.finish()
