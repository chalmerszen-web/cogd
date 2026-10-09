"""Audit a bounded failed acoustic experiment and verified application rollback."""
import hashlib
import json
from pathlib import Path

ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'artifacts/kws-phase4'


def read(path):return json.loads(path.read_text(encoding='utf8'))
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    selected=read(ROOT/'artifacts/kws-phase3/selected-model.json')
    assert all(sha(ROOT/name)==digest for name,digest in selected['files'].items())
    assert sha(ROOT/'components/kws_c11/generated/trained.c')==selected['files']['artifacts/kws-phase3/int8/model.c']
    positive=read(OUT/'formal/replay.json');negative=read(OUT/'formal-negatives/replay.json')
    assert len(positive['trials'])==40 and len(negative['trials'])==20 and negative['complete']
    assert positive['error'].startswith('TypeError') and not positive['complete']
    expected=read(OUT/'formal/selection.json')
    actual=positive['trials']+negative['trials']
    assert [r['clip_id'] for r in actual]==[r['clip_id'] for r in expected]
    assert len(set(r['clip_id'] for r in actual))==60
    for report,directory in [(positive,'formal'),(negative,'formal-negatives')]:
        assert report['script_sha256']==sha(OUT/directory/'acoustic_test.py')
        assert report['settings']['threshold']==920
        assert report['settings']['gain']==.35 and report['settings']['source_rms']==.14
        assert all(t['before']['state']=='listening' and t['after']['dma_lost']==t['before']['dma_lost']
                   and t['after']['wakes']>=t['before']['wakes'] for t in report['trials'])
    languages={}
    for language in ('zh','yue'):
        trials=[t for t in positive['trials'] if t['language']==language]
        assert len(trials)==20
        languages[language]=dict(total=20,triggers=sum(t['triggered'] for t in trials),
                                valid_hits=sum(t['valid_hit'] for t in trials))
    parity=read(OUT/'parity-final/board-parity.json')
    assert parity['complete'] and parity['frames']==512 and not parity['mismatches']
    flash=read(OUT/'flash-default-threshold.log');rollback=read(OUT/'rollback.log')
    for r in (flash,rollback):
        assert r['installed'] and r['application_verified'] and r['data_partitions_unchanged']
        backup=Path(r['directory'])
        assert sha(backup/'flash-before.bin')==r['backup_sha256']
    assert flash['new_application_sha256']==sha(ROOT/'build-kws-trained/esp_hi_agent.bin')
    assert rollback['new_application_sha256']=='3999e26330fb7ea49b7bb32dbbaa3ab3bae0662263176d11b9480e734f4ff263'
    restored=read(OUT/'rollback-state.json')
    assert restored['agent status']['version']=='0.6.3-context' and restored['context_preserved']
    assert restored['agent status']['wifi'] and not restored['agent wake status']['enabled']
    assert restored['agent context stats']==read(OUT/'before-rollback.json')['agent context stats']
    result=dict(outcome='recognition_failed_rolled_back',languages=languages,
        negatives=dict(total=20,observed_triggers=negative['negatives']['triggers'],
                       acceptance_valid=False,limitation='Post-playback tail was not uniformly observed'),
        board_parity=dict(frames=512,mismatches=0,max_frame_us=parity['max_us']),
        negative_phase_resources=negative['resources'],
        frozen_model_unchanged=True,experimental_application=flash['new_application_sha256'],
        restored_application=rollback['new_application_sha256'],
        deferred_by_failure_exit=['30-minute observation','full experimental Agent regression'],
        recordings='calibration-final/',report='docs/WAKE_XIAOYAN_PHASE4_REPORT.md')
    (OUT/'results.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf8')
    audit=dict(bounded_diagnostic_complete=True,recognition_acceptance_passed=False,
        stop_rule='Approved plan stops on recognition failure; no further training or long stability campaign',
        evidence_checks=['frozen weights','fixed sample identities','threshold and gain','executed script hashes',
                         'board parity','verified backups','application-only rollback','live restored identity','context retention'],
        limitations=['synthetic replay, not human generalization','negative observation window insufficient',
                     'one unscored negative playback after harness exception','small ADC clipping counts retained'],
        result_sha256=sha(OUT/'results.json'))
    (OUT/'completion-audit.json').write_text(json.dumps(audit,indent=2),encoding='utf8')
    print(json.dumps(result,ensure_ascii=False))


if __name__=='__main__':main()
