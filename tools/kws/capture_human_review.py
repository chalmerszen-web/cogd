"""User-operated local reference capture; never starts training or claims distance."""
import argparse
from datetime import datetime
import json
from pathlib import Path
import re
import time

ROOT=Path(__file__).resolve().parents[2]


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--language',choices=('zh','yue','both'))
    parser.add_argument('--distance-m',type=float)
    parser.add_argument('--speaker',help='Anonymous stable speaker ID, reused at every distance')
    parser.add_argument('--repeats',type=int,choices=range(1,6),default=3)
    parser.add_argument('--delay',type=int,choices=range(3,31),default=10)
    args=parser.parse_args()
    language=args.language or {'1':'zh','2':'yue','3':'both'}.get(input('选择能自然说的语言：1 普通话，2 粤语，3 两种：').strip())
    if language not in ('zh','yue','both'):raise ValueError('语言选项无效')
    distance=args.distance_m if args.distance_m is not None else float(input('请实测嘴到设备麦克风的距离（米，例如 0.5 或 5）：'))
    if not .2<=distance<=10:raise ValueError('距离必须在0.2到10米之间')
    speaker=args.speaker or input('说话人匿名编号（如 p01；同一人在所有距离使用同一编号）：').strip()
    if not re.fullmatch(r'[A-Za-z0-9_-]{1,32}',speaker):raise ValueError('编号限1到32位字母、数字、下划线或短横线')
    print('请先关闭串口窗口和其他播放声。绿灯亮起后说指定句子，熄灭后停止。')
    print('录音只保存在本机；这批材料用于独立核验，不会自动用于训练。')
    input('准备好后按回车开始；随时按 Ctrl+C 结束。')
    # Hardware imports are deferred so --help is read-only and portable.
    from serial_test import Link
    from calibrate_acoustic import export_clip
    out=ROOT/'artifacts/kws-human-review'/datetime.now().strftime('%Y%m%d-%H%M%S')
    out.mkdir(parents=True,exist_ok=False)
    report=dict(complete=False,distance_m_user_reported=distance,distance_verified_by_software=False,
                intended_languages=language,speaker_id=speaker,source_group='human-'+speaker,
                trials=[],training_allowed=False,started=time.time())
    link=None;light=None
    try:
        link=Link('COM5',out/'serial.log')
        report['device']=link.command('agent status');report['wake']=link.off()
        light=link.command('agent light get')
        report['previous_clip']=export_clip(link,out/'previous-clip.wav')
        languages=('zh','yue') if language=='both' else (language,)
        for lang in languages:
            title='普通话' if lang=='zh' else '粤语'
            for repeat in range(args.repeats):
                for label,phrase in ((1,'你好，小言'),(0,'你好，小燕')):
                    print(f'\n{title}，第{repeat+1}组：请说「{phrase}」。')
                    input(f'按回车后有{args.delay}秒就位；等绿灯再说。')
                    link.command('agent light set 20 12 0');time.sleep(args.delay)
                    link.command('agent audio capture 5000');deadline=time.monotonic()+12
                    while time.monotonic()<deadline:
                        ready=link.command('agent audio status')
                        if ready['recording'] and ready['capture_stage']==2:break
                        time.sleep(.05)
                    else:raise TimeoutError('麦克风未就绪')
                    link.command('agent light set 0 20 0');print('绿灯：现在说。',flush=True)
                    deadline=time.monotonic()+10
                    while time.monotonic()<deadline:
                        state=link.command('agent audio status')
                        if not state['recording'] and state['clip_ready']:break
                        time.sleep(.1)
                    else:raise TimeoutError('录音未完成')
                    link.command('agent light set 0 0 0')
                    assert state['capture_error']=='ok' and state['clip_ms']==5000
                    name=f'{len(report["trials"]):02d}-{lang}-{label}.wav'
                    signal=export_clip(link,out/name)
                    trial=dict(path=name,language=lang,intended_text=phrase,intended_label=label,
                               speaker_confirmed=False,confirmation_pending=True,phonetics_independently_verified=False,
                               ready=ready,audio_state=state,signal=signal)
                    report['trials'].append(trial)
                    (out/'report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
                    trial['speaker_confirmed']=input('是否完整说了指定句子？直接回车确认；输 n 标记作废：').strip().lower() in ('','y','yes','是')
                    trial['confirmation_pending']=False
                    (out/'report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
        report['complete']=True
    except BaseException as error:
        report['error']=repr(error);raise
    finally:
        report['cleanup_errors']=[]
        cleanup=['agent audio stop','agent wake off'] if link else []
        if light:cleanup.append('agent light set {r} {g} {b}'.format(**light))
        for command in cleanup:
            try:link.command(command)
            except Exception as error:report['cleanup_errors'].append(repr(error))
        if link:
            try:link.close()
            except Exception as error:report['cleanup_errors'].append(repr(error))
        report['ended']=time.time()
        (out/'report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
        print(f'\n材料已保存：{out}')


if __name__=='__main__':main()
