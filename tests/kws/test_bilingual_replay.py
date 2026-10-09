"""Matched acoustic comparisons must reject incomplete or mismatched evidence."""
import copy
import sys
from pathlib import Path
import pytest

sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'tools/kws'))
from bilingual_replay import compare


def report(pair):
    return dict(complete=True,pair=pair,manifest_sha256='same-manifest',script_sha256='same-script',settings={'gain':.35},
        source_files=[dict(clip_id=str(i),wav_sha256=str(i)) for i in range(14)],
        identity={'device_id':'same-board'},output_name='same-speaker',languages={},negatives={},resources={},
        trials=[dict(clip_id=str(i),label=int(i<8),language='zh' if i<4 else 'yue',
            text='source',triggered=i<8,valid_hit=i<8) for i in range(14)])


def test_matched_replay_reports_both_improvements_and_regressions():
    before,after=report('ek'),report('el')
    before['trials'][0].update(triggered=False,valid_hit=False)
    after['trials'][9].update(triggered=True)
    result=compare(before,after)
    assert result['complete'] and result['matched_trials']==14
    assert [r['clip_id'] for r in result['changed']]==['0','9']
    assert result['changed'][1]['label']==0 and result['changed'][1]['after_triggered']


@pytest.mark.parametrize('change', ['incomplete','count','pair','settings','manifest','script','device','speaker','sources'])
def test_mismatched_or_incomplete_run_cannot_be_compared(change):
    before,after=report('ek'),report('el')
    if change=='incomplete':after['complete']=False
    elif change=='count':after['trials'].pop()
    elif change=='pair':after['pair']='ek'
    elif change=='settings':after['settings']['gain']=.5
    elif change=='manifest':after['manifest_sha256']='different'
    elif change=='script':after['script_sha256']='different'
    elif change=='device':after['identity']['device_id']='another-board'
    elif change=='speaker':after['output_name']='another-speaker'
    elif change=='sources':after['source_files'][0]['wav_sha256']='different-audio'
    with pytest.raises(ValueError):compare(before,after)
