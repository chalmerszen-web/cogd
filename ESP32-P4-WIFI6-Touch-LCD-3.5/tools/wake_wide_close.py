"""Close the bounded wide95 experiments without converting failed gates into a release."""
import datetime, hashlib, json
from pathlib import Path

P = Path(__file__).resolve().parents[1]
B = P / 'wake_training/20261101/wide95'
PACKAGE = P / 'firmware/experimental-wide95-mega500-20261101'
GROUPS = ('nihao_zh', 'hi_zh', 'nihao_yue', 'hi_yue')
NAMES = ('普通话“你好，小言”', '普通话“Hi，小言”', '粤语“你好，小言”', '粤语“Hi，小言”')


def read(path):
    return json.loads(Path(path).read_text(encoding='utf8'))


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def write(path, value):
    Path(path).write_text(json.dumps(value, ensure_ascii=False, indent=2) + '\n', encoding='utf8')


def main():
    acoustic = read(B / 'acoustic-mega500-ordinary/report.json')
    assert acoustic['complete'] and len(acoustic['trials']) == 40
    assert acoustic['candidate_app_sha256'] == sha(PACKAGE / 'cogd_p4.bin')
    candidate_readback = read(B / 'mega500-ordinary-readback.json')
    assert candidate_readback['complete'] and candidate_readback['match']
    assert candidate_readback['app_sha256'] == acoustic['candidate_app_sha256']
    assert acoustic['manifest_sha256'] == sha(B / 'development-weak.jsonl')
    assert all(t['source']['split'] == 'validation' for t in acoustic['trials'])
    assert all(t['before']['threshold'] == 500 and t['before']['input_gain'] == 1 for t in acoustic['trials'])
    assert all(t['before']['dma_lost'] == t['after']['dma_lost'] for t in acoustic['trials'])
    groups = acoustic['groups']
    false_clips = [{'clip_id': t['source']['clip_id'], 'text': t['source']['text'],
                    'phonetic_tokens': t['source'].get('phonetic_tokens')} for t in acoustic['trials']
                   if not t['source']['label'] and t['hit']]
    passed = all(groups[g]['triggers'] == groups[g]['trials'] == 6 for g in GROUPS) and groups['negative']['triggers'] == 0
    assert not passed, 'A passing candidate needs the untouched final gate; this failed-experiment closer cannot release it'
    final = read(B / 'final-restored-r2/status.json')
    readback = read(B / 'final-restored-r2-readback.json')
    assert final['complete'] and readback['complete'] and all(i['match'] for i in readback['images'])
    assert final['system']['version'] == '0.2.0-p4-wake4' and final['system']['wifi']
    assert final['hardware']['cpu_mhz'] == 200 and final['voice']['voice_enabled'] and final['wake']['state'] == 'listening'
    corpus = read(B / 'corpus-audit.json')
    independence = read(B / 'independence-live-c.json')
    assert independence['complete'] and independence['independent_acceptance_not_scored']
    assert sha(B / 'test-frozen-sources.jsonl') == independence['test_manifest_sha256']
    assert sha(B / 'test-prepared/manifest.jsonl') == independence['test_prepared_sha256']
    training = read(B / 'train-mega/training-report.json')
    for head in training['heads']:
        assert sha(PACKAGE / 'models' / (head['head'] + '.c')) == head['model_c_sha256']
    validation = read(B / 'validation-mega/validation.json')
    assert validation['complete'] and all(r['exact'] for r in validation['parity'])
    memory = read(B / 'memory-mega500-ordinary.json')
    memory['device'] = {
        'model_heap_bytes': sorted({t['before']['model_heap'] for t in acoustic['trials']}),
        'workspace_bytes_in_model_heap': sorted({t['profile_after']['workspace'] for t in acoustic['trials']}),
        'max_block_us': max(t['profile_after']['max_us'] for t in acoustic['trials']),
        'block_duration_us': 32000, 'cpu_mhz': 200,
        'scope': 'Actual ordinary firmware DEVELOPMENT playback; excludes other subsystem buffers and thread stacks',
    }
    assert memory['device']['model_heap_bytes'] == [35972] and memory['device']['workspace_bytes_in_model_heap'] == [35208]
    curves = {}
    for name in ('curve-baseline-r2', 'curve-wide2-sync', 'curve-wide3-sync', 'curve-mega', 'curve-contrast',
                 'curve-mega-augment', 'curve-mega-ns', 'curve-mega-ns-adapt', 'curve-mega-clean', 'word256-development'):
        r = read(B / name / 'report.json')
        assert r['complete']
        curves[name] = {'scope': r['scope'], 'passing_settings': r['passing_settings'],
                        'thresholds': {k: {'groups': v['groups'], 'passed': v['passed']} for k, v in r['thresholds'].items()}}
        assert not r['passing_settings']
    ctc = {name: read(B / name / 'report.json')['groups'] for name in ('ctc96-development', 'ctc96-deadline-development', 'ctc96-clean-development')}
    multinet = {}
    for name in ('multinet-pilot-gain1-v3', 'multinet-pilot-gain4-v3', 'multinet-clean-pilot-v3'):
        r = read(B / name / 'report.json'); assert r['complete']
        multinet[name] = {'groups': r['groups'], 'gain': r['gain'], 'threshold': r['threshold'],
            'max_block_us': max(t['end']['max_us'] for t in r['trials']),
            'internal_heap_bytes': sorted({t['begin']['internal_heap'] for t in r['trials']}),
            'psram_heap_bytes': sorted({t['begin']['psram_heap'] for t in r['trials']}),
            'scope': r['scope'], 'app_sha256': r['app_sha256']}
    result = {
        'status': 'FAILED_95_PERCENT_AND_FALSE_WAKE_GATES',
        'created_utc': datetime.datetime.now(datetime.timezone.utc).isoformat(),
        'board': 'ESP32-P4-WIFI6-Touch-LCD-3.5, 16 MB Flash, 32 MB PSRAM, 200 MHz',
        'corpus': corpus, 'physical_development': {'groups': groups, 'conditions': acoustic['conditions'],
            'scope': acoustic['scope'], 'app_sha256': acoustic['candidate_app_sha256'], 'candidate': '96/96/48 INT8, threshold500, no NS',
            'false_clips': false_clips},
        'recorded_development_curves': curves, 'ctc_development': ctc, 'multinet_probe': multinet,
        'memory': memory, 'independence': independence,
        'gates': {'development_95_and_no_false': 'FAIL', 'independent_final_168': 'NOT RUN',
            'five_minute_background': 'NOT RUN', 'four_quiet_dialogues': 'NOT RUN', 'human_validation': 'NOT RUN',
            'integer_parity': 'PASS', 'candidate_app_readback': 'PASS', 'restored_r2_readback': 'PASS', 'restored_r2_idle': 'PASS'},
        'current_device': {'version': final['system']['version'], 'voice': True, 'wifi': True,
            'wake': final['wake']['state'], 'threshold': final['wake']['threshold'],
            'model_heap': final['wake']['model_heap'], 'workspace': final['profile']['workspace'],
            'firmware': 'firmware/experimental-wake4-three-rounds-20261101',
            'scope': 'Previous ordinary dialogue candidate restored, not newly accepted'},
        'readback': readback, 'candidate_readback': candidate_readback,
        'maximum_achievable_recall': 'Unknown. No reliable population ceiling can be inferred from this synthetic fixed-position experiment.',
        'evidence_sha256': {},
    }
    evidence = ['corpus-audit.json', 'independence-live-c.json', 'train-mega/training-report.json', 'validation-mega/validation.json',
        'acoustic-mega500-ordinary/report.json', 'memory-mega500-ordinary.json', 'final-restored-r2/status.json',
        'final-restored-r2-readback.json', 'mega500-ordinary-readback.json', 'host-regression-phase5.log', 'pcm-ownership-tests-v2.log', 'c3-kernel-preservation.json']
    result['evidence_sha256'] = {name: sha(B / name) for name in evidence}
    write(B / 'result.json', result)
    manifest = read(PACKAGE / 'manifest.json')
    manifest['physical_ordinary_rerun'] = {'status': 'FAIL', 'groups': groups, 'report': '../../wake_training/20261101/wide95/acoustic-mega500-ordinary/report.json'}
    manifest['installed_at_close'] = False
    manifest['linked_model_constants_bytes'] = memory['candidate']['linked_constant_bytes']
    manifest['device_model_memory'] = memory['device']
    manifest['independent_app_readback'] = candidate_readback
    write(PACKAGE / 'manifest.json', manifest)
    old_progress = B / 'progress-before-close.json'
    if not old_progress.exists(): old_progress.write_bytes((B / 'progress.json').read_bytes())
    write(B / 'progress.json', {k: result[k] for k in ('status', 'created_utc', 'board', 'gates', 'current_device')})
    physical_lines = []
    for g, name in zip(GROUPS, NAMES):
        v = groups[g]; physical_lines.append(f'| {name} | {v["triggers"]}/{v["trials"]} | {v["triggers"]/v["trials"]:.1%} |')
    curve_lines = []
    for label, name, setting in [('此前 R2', 'curve-baseline-r2', '740'), ('96/96/48，原阈值', 'curve-mega', '740'),
                                 ('96/96/48，低阈值', 'curve-mega', '500'), ('256 通道，浮点实验', 'word256-development', '500')]:
        gs = curves[name]['thresholds'][setting]['groups']
        values = [f'{gs[g]["hits"]}/{gs[g]["count"]}' for g in GROUPS]
        neg = gs['negative']; false_count = neg.get('false_events', neg.get('false'))
        curve_lines.append('| ' + ' | '.join([label + ' / ' + setting, *values, f'{false_count}/{neg["count"]}']) + ' |')
    text = f'''# 四组唤醒词 95% 目标：扩大模型与弱音实验

本轮**未通过**四组各 ≥95% 且控制误唤醒的门槛，不是可交付验收的成功版本。临时诊断固件已退出，实物恢复此前普通联网对话候选 `0.2.0-p4-wake4`；本轮没有请用户采集真人录音，也没有请求用户验收。

## 最后一次普通固件实物回放

使用 96/96/48 INT8 模型、阈值 500、输入增益 1、200 MHz，关闭降噪实验和 PCM 诊断。新进行 40 次开发回放，全部保留，不重试覆盖失败。电脑通过现有 Misiom-Shooter 输出，P4 的实际麦克风输入并产生唤醒动作；语音对话暂时关闭以便逐条计数。这是开发测试，不能称为独立最终成功率。

| 发音 | 成功 / 回放 | 比例 |
|---|---:|---:|
{chr(10).join(physical_lines)}

反例误触发 **{groups['negative']['triggers']}/{groups['negative']['trials']}**，本次误触发内容为 {'、'.join(r['text'] for r in false_clips) or '无'}。反例包括近音人名及非唤醒语句，不能换算成每小时误唤醒率。每组只有 3 条源录音，在 0.06 数字播放增益和 0.10 增益的滤波／短反射条件下各一次；不是 6 个独立说话人。固定了声源与板子位置，没有测量声压、距离或实际角度，不能等同于用户在安静环境、20–50 厘米讲话的成功率。

预设开发门槛要求每组 6/6、反例 0/16。本轮未通过，因此新冻结的 56 条源录音、168 次最终测试保持未评分；5 分钟非目标背景和四轮弱音完整 AI 对话也未作为本候选验收继续执行。此前版本的对话成功记录不能代替本候选。

## 在同一批固定麦克风录音上比较模型

下面是完全相同的 40 条已保存开发录音。前三行使用与固件对应的 C 整数推理及决策规则；最后一行是尚未部署的浮点模型。它们用于诊断和选模，与上面的新实物回放是不同实验，不能合并分母。

| 模型／阈值 | 普通话你好 | 普通话 Hi | 粤语你好 | 粤语 Hi | 反例误触发 |
|---|---:|---:|---:|---:|---:|
{chr(10).join(curve_lines)}

扩大模型确实提高了这批录音的召回，但低阈值同时增加误触发。已检查的阈值范围 500–950 内，没有同时通过四组 95% 和零反例门槛的候选。普通话“小言”和“小燕”等近音词仍是问题，不能用“提高灵敏度”掩盖。

## 数据与改动

- 千问本轮成功生成 **{corpus['generated_total']} 条唯一录音**；按实际成功请求编号去重，重用的一条探针不重复计数。清楚标注为系统合成音色，没有把音高变化算成新说话人。
- 自动读音筛查后新增 TRAIN 来源 **{corpus['admitted_new_train_sources']} 条、{corpus['admitted_new_train_voice_count']} 种音色**；其中普通话你好 69、普通话 Hi 83、粤语你好 13、粤语 Hi 11、反例 221。保留被筛除的录音及原因。粤语合成读音不稳定，不能只依据请求中的方言和拼音参数当作正确发音。
- 普通话开发音色 Bella/Stella、最终音色 Jennifer/Ryan/Katerina/Aiden 与新增训练音色分开；粤语 Rocky/Kiki 只保证录音分开，音色已见过，不能声称粤语说话人独立。
- 依次测试 48/48/48 和 96/96/48、弱音及声道扩增、同步确认、不同前端、降噪再训练、CTC 音素模型、清理旧来源与截断负例，以及 256 通道五分类模型。各失败结果保留在 `wake_training/20261101/wide95`。
- 修复 P4 旧确认策略使用已过期初筛支持分数的问题，改成当前初筛与复核支持重合；只有真正接受的唤醒才消耗冷却。私有 P4 推理分支保留 C3 默认内核；22 个来源内核／头文件与建立分支时的哈希一致。
- 三个 INT8 头的 300 帧随机／极值 C 与 Python 整数结果逐项一致；实际 P4 PCM 回放也核对了每块分数及事件。共享主机回归 96/96，通过后 PCM 所有权改动另有两项定向检查通过。数值一致性不代表识别率通过。

## 为什么继续加大还没有解决

实际弱音录音与干净合成源存在明显分布差异；部分粤语片段的“含语音区间总能量／前段背景能量”比值很低。该比值不是真实 SNR 或声压。输入量化饱和很少，数字增益 2/4 倍及浮点推理没有解决问题；不能将漏检主要归因于 INT8 计算差异。弱音泛化、发音覆盖和近音声调区分仍需改进。

WebRTC 降噪实验增加约 41,464 B 堆及 640 B 静态 PCM 缓冲，但大量弱音正例同时被抑制；重新训练也没有达到门槛，因此没有启用它作为默认。清除未可靠核对的旧语音及按时间截断的片段后重新训练，结果仍失败，不能据此断言只要清洗标签就能通过。

另实测乐鑫 MultiNet7 中文预训练模型：打包权重与词表 **2,671,766 B**，临时 P4 工程新增约 **20,360 B 内部堆、2,931,532–2,931,788 B PSRAM 堆**，检测单块峰值 **30,627 µs / 32,000 µs**。内存装得下，但 CPU 余量很小。四条弱音 TRAIN 正例在增益 1 和 4 下都未识别；对应干净数字源的两条普通话识别、两条粤语未识别。这里只是小样本接入诊断，不能当作群体成功率。中文词表无声调符号，粤语仅用了实验性的普通话拼音近似；没有接入正常唤醒。模型接口参考[乐鑫 MultiNet 官方文档](https://docs.espressif.com/projects/esp-sr/en/latest/esp32p4/speech_command_recognition/README.html)，以上资源数值来自本板实测。独立千问盲听对八条实际麦克风片段全部标为不确定，并出现错误转写，其输出未用于回改标签，也不作为噪声来源的事实判断。

## 内存与速度

四种发音共用模型，以下不是每种词分别占用，也不是整机内存。

| 配置 | 唤醒运行堆 | 其中推理工作区 | 模型与静音种子常量 |
|---|---:|---:|---:|
| 此前 24/24/48 | 17,860 B | 17,096 B | 48,923 B |
| 本轮 96/96/48 | 35,972 B | 35,208 B | 179,499 B |

扩大后运行堆增加 18,112 B，常量增加 130,576 B。工作区已包含在运行堆中，不能再次相加。常量位于 Flash rodata，当前配置还复制／映射到 PSRAM，不等于内部 SRAM 工作区。本轮普通固件实物回放观测的单块最大耗时 **{memory['device']['max_block_us']} µs**，32 ms 输入周期，未观测到 DMA 丢块；这不是所有运行场景的最坏时间保证。麦克风 DMA、栈、显示、网络和 AI 对话缓冲未计入上述“唤醒模型内存”。

256 通道模型含 368,133 个训练参数（含 BatchNorm），开发集误触发过多，未量化部署；没有它的实机内存与耗时数据。不能把参数量当作设备占用。

## 交付状态与继续优化的代价

当前实物保留此前 R2 普通固件，三段 Flash 独立读回匹配，联网、语音和监听恢复，30 秒空闲复核通过。本轮 96 通道普通实验包在 `firmware/experimental-wide95-mega500-20261101`，明确标为 FAILED，不是已通过验收的升级。模型、训练脚本、数据清单、全部失败及诊断工程均保留，Key 不进入技能包或报告。

千问本地 skill 已安装在 `C:/Users/PC/.codex/skills/qianwen-model-suite`，可分发包为 `tools/skill_packages/qianwen-model-suite-1.1.0.zip`；运行时从已配置的 Key 文件读取凭据。

仍可继续优化，但目前无法计算可信的“最高能到多少”。95% 是目标，不是已达到或可保证的上限。下一步有依据的方向是改善输入声道／有效语音与底噪比例，以及引入能够迁移到普通话和粤语的预训练声学特征并独立处理近音声调；仅复制更多通道已显示收益递减。代价分别是声学结构或前端改动、额外合成与筛查费用、训练适配工作，以及更高 RAM、Flash、推理时间和功耗。降低阈值的代价已经实测为误触发增加；通用云端转写也不能自动解决低信噪比与近音词问题。

完整机器可读证据：[result.json](wake_training/20261101/wide95/result.json)。原三轮与更早记录：[WAKE4_REPORT.md](WAKE4_REPORT.md)。最终测试保持封存，没有以开发集或训练命中率冒充 95% 验收结果。
'''
    (P / 'WAKE95_REPORT.md').write_text(text, encoding='utf8')
    print(json.dumps({'status': result['status'], 'physical_groups': groups, 'report': str(P / 'WAKE95_REPORT.md')}, ensure_ascii=True))


if __name__ == '__main__':
    main()
