"""Admit phonetic content before any candidate scoring; keep all decisions."""
import argparse, collections, hashlib, json, re
from pathlib import Path
from wake_prepare import ROOT

def sounds(value):
    if isinstance(value,list):value=' '.join(map(str,value))
    return re.findall(r'[a-z]+[1-9]?',str(value).lower())

def positive(tokens,language,hi):
    suffix=['xiao3','yan2'] if language=='zh' else ['siu2','jin4']
    if tokens[-2:]!=suffix:return False
    if hi:return len(tokens)==3 and tokens[0] in ('hi','hi1','hai1','hai3','haai1','hoi1','ha')
    return len(tokens)==4 and tokens[:2] in ([['ni3','hao3'],['ni2','hao3']] if language=='zh' else [['nei5','hou2']])

def main():
    p=argparse.ArgumentParser();p.add_argument('--base',type=Path,required=True)
    p.add_argument('--private',type=Path,default=ROOT/'ESP32-P4-WIFI6-Touch-LCD-3.5/.local/wide95-review');a=p.parse_args();b=a.base.resolve()
    private=a.private.resolve()
    observations={}
    for path in sorted((b/'blind-review').glob('mapping-*.json')):
        mapping=json.loads(path.read_text());result=private/path.name.replace('mapping-','batch-').replace('.json','')/'result.txt'
        text=result.read_text(encoding='utf8');items=json.loads(text[text.index('['):text.rindex(']')+1])
        assert len(items)==len(mapping)
        for m,o in zip(mapping,items):
            assert o['index']==m['index'];observations[m['clip_id']]=o
    rows=[]
    for i in range(4):rows += list(map(json.loads,(b/f'corpus-{i}/manifest.jsonl').read_text(encoding='utf8').splitlines()))
    accepted=[];decisions=[]
    for row in rows:
        o=observations[row['clip_id']];tokens=sounds(o['phonemes']);reason=''
        if row['label']:
            good=positive(tokens,row['language'],row['wake_group'].startswith('hi'))
            reason='exact target phonemes, including name tone' if good else 'target phonemes or requested dialect not confirmed'
        else:
            is_target=any(positive(tokens,lang,hi) for lang in ('zh','yue') for hi in (False,True))
            if is_target:good=False;reason='generated negative sounds like an actual target'
            elif any(name in row['text'] for name in ('小燕','小杨','小王','小云')):
                tail={'zh':{'小燕':(['xiao3','yan4'],),'小杨':(['xiao3','yang2'],),'小王':(['xiao3','wang2'],),'小云':(['xiao3','yun2'],)},
                      'yue':{'小燕':(['siu2','jin3'],['siu2','jin6'],['siu2','yin6']),'小杨':(['siu2','joeng4'],),'小王':(['siu2','wong4'],),'小云':(['siu2','wan4'],)}}
                name=next(n for n in tail[row['language']] if n in row['text'])
                good=tokens[-2:] in tail[row['language']][name];reason='independently transcribed contrasting name phonemes' if good else 'near-name pronunciation unconfirmed'
            elif row['text'].strip('。！! ,，')=='小言':
                good=tokens in (['xiao3','yan2'],['siu2','jin4']);reason='name alone without greeting'
            elif row['text'].strip('。！! ,，').lower() in ('hi','你好'):
                good=len(tokens)<=2 and bool(tokens);reason='short greeting without name'
            else:good=bool(tokens) and len(tokens)>=3;reason='independently transcribed non-target sentence'
        decision={'clip_id':row['clip_id'],'split':row['split'],'accepted':good,'reason':reason,'tokens':tokens,'review':o}
        decisions.append(decision)
        if good:accepted.append(dict(row,phonetic_audit='blind Qwen3.8 Omni, target not in prompt',phonetic_tokens=tokens))
    for split in ('train','validation','test'):
        selected=[r for r in accepted if r['split']==split];path=b/(split+'-phonetic.jsonl')
        path.write_text(''.join(json.dumps(r,ensure_ascii=False)+'\n' for r in selected),encoding='utf8')
        print(json.dumps({'split':split,'count':len(selected),'groups':dict(collections.Counter(r['wake_group'] for r in selected))}),flush=True)
    (b/'phonetic-admission.json').write_text(json.dumps({'source_count':len(rows),'admitted':len(accepted),'decisions':decisions,
        'script_sha256':hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),'kws_scores_consulted':False,
        'uncertain_name_spelling_allowed_only_with_exact_phonemes':True,'human_verified':False},ensure_ascii=False,indent=2),encoding='utf8')

if __name__=='__main__':main()
