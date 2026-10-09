"""Ensure failed/inactive board observations cannot look like real-time success."""
from pathlib import Path
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'tools/kws'))
from acoustic_test import resource_summary,select_negatives,observation_complete


def test_actual_inferred_duration_excludes_wall_clock_pauses():
    samples=[dict(wake={'state':'listening'},status={'min_heap':40000,'largest_block':30000})]
    profile=dict(blocks=100,max_us=5200,bin_us=500,histogram=[0]*10+[100]+[0]*53)
    result=resource_summary(samples,profile)
    assert result['inferred_audio_seconds']==3.2
    assert result['p99_us_upper_bound']==5500
    assert not result['failures']
    profile.update(max_us=33000)
    assert 'inference deadline' in resource_summary(samples,profile)['failures']


def test_no_audio_or_no_listener_is_not_a_pass():
    profile=dict(blocks=0,max_us=0,bin_us=500,histogram=[0]*64)
    assert set(resource_summary([],profile)['failures'])=={'no inferred audio','no listening samples'}


def test_negative_subset_covers_both_languages_and_all_categories():
    rows=[]
    for language in ('zh','yue'):
        for text in ('你好','小言','嗨乐鑫','你好乐鑫','你好小王','你好小燕','你好小杨','请把灯打开','现在几点钟','播放音乐'):
            for voice in range(2):
                rows.append(dict(clip_id=f'{language}-{text}-{voice}',language=language,text=text))
    selected=select_negatives(rows,20)
    assert len(selected)==len({r['clip_id'] for r in selected})==20
    assert sum(r['text'] in ('你好','小言') for r in selected)==7
    assert {r['language'] for r in selected if r['text']=='嗨乐鑫'}=={'zh','yue'}
    assert sum(r['text'] in ('请把灯打开','现在几点钟','播放音乐') for r in selected)==5


def test_negative_without_word_boundary_keeps_post_playback_tail():
    audio=dict(source_started=10.,finished=12.)
    for row in ({},{'wake_end_sample':None},{'wake_end_sample':16000}):
        assert not observation_complete(audio,row,12.1)
        assert not observation_complete(audio,row,13.1)
        assert observation_complete(audio,row,13.3)
