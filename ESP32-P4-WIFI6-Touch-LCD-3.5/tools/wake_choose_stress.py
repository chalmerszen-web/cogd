"""Choose among exactly three development rounds without opening final results."""
import hashlib, json, shutil
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
T=ROOT/'ESP32-P4-WIFI6-Touch-LCD-3.5/wake_training/20261101/three-rounds'
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def main():
    assert not (T/'final-frozen/report.json').exists(), 'Select before the final replay'
    assert json.loads((T/'training-independence.json').read_text())['complete']
    rows=[]
    for i in (1,2,3):
        path=T/f'acoustic-r{i}/report.json';report=json.loads(path.read_text(encoding='utf8'))
        assert report['complete'] and len(report['trials'])==36
        assert report['manifest_sha256']==sha(T/'development-stress/manifest.jsonl')
        assert report['candidate_app_sha256']==sha(T/f'candidate-r{i}-build/cogd_p4.bin')
        groups=report['groups'];recalls=[groups[g]['triggers']/groups[g]['trials'] for g in ('nihao_zh','hi_zh','nihao_yue','hi_yue')]
        negative=groups['negative'];specificity=1-negative['triggers']/negative['trials']
        rows.append({'round':i,'groups':groups,'conditions':report['conditions'],
                     'macro_recall':sum(recalls)/4,'specificity':specificity,
                     'balanced_score':.5*(sum(recalls)/4+specificity),'worst_recall':min(recalls),
                     'report_sha256':sha(path),'app_sha256':report['candidate_app_sha256']})
    chosen=max(rows,key=lambda r:(r['balanced_score'],r['worst_recall'],-r['groups']['negative']['triggers'],r['round']))
    result={'complete':True,'selected_round':chosen['round'],'selected_app_sha256':chosen['app_sha256'],
            'rule':'Half macro recall plus half negative specificity; ties worst recall, fewer false wakes, later round',
            'final_test_used':False,'rounds':rows,'plan_sha256':sha(T/'three-round-plan.json'),
            'frozen_manifest_sha256':sha(T/'frozen-stress/manifest.jsonl')}
    output=T/'selection.json';assert not output.exists()
    output.write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf8')
    # The final test hashes the default build image. Restore those artifacts
    # before the caller flashes a selected earlier round's immutable snapshot.
    project=ROOT/'ESP32-P4-WIFI6-Touch-LCD-3.5'
    if chosen['round'] != 3:
        for name in ('cogd_p4.bin','cogd_p4.elf','cogd_p4.map','bootloader/bootloader.bin','partition_table/partition-table.bin'):
            shutil.copy2(T/f"candidate-r{chosen['round']}-build"/name,project/'build'/name)
    assert sha(project/'build/cogd_p4.bin')==chosen['app_sha256']
    print(json.dumps(result,ensure_ascii=False))
if __name__=='__main__':main()
