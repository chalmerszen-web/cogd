"""Update existing project documentation from archived, completed evidence."""
import json
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
P=ROOT/'ESP32-P4-WIFI6-Touch-LCD-3.5'
E=P/'evidence/wake4-three-rounds-20261101'
GROUPS=('nihao_zh','hi_zh','nihao_yue','hi_yue')
NAMES=('普通话“你好，小言”','普通话“Hi，小言”','粤语“你好，小言”','粤语“Hi，小言”')
def ratio(g):return f"{g['triggers']}/{g['trials']}（{100*g['triggers']/g['trials']:.1f}%）"
def row(name,groups):return '| '+name+' | '+' | '.join(ratio(groups[g]) for g in (*GROUPS,'negative'))+' |'
def main():
    r=json.loads((E/'result.json').read_text(encoding='utf8'));m=r['memory']
    accepted=r['status']=='PASS_FIXED_SYNTHETIC_SCOPE'
    status='本轮固定合成回放验收通过，真人体验尚未验证。' if accepted else '三轮优化已完成，但仍未通过全部预设验收；当前保留最佳开发候选，不能称为已调好。'
    comparison='\n'.join([row('修改前 v11',r['baseline'])]+[row(f"本轮 R{x['round']}",x['groups']) for x in r['rounds']])
    final='\n'.join('| '+name+' | '+ratio(r['final_groups'][g])+' |' for g,name in zip(GROUPS,NAMES))
    conditions='\n'.join(row(name,r['final_conditions'][condition]) for condition,name in [('quiet','低音量 0.10'),('very_quiet','更低音量 0.06'),('filtered_reflections','滤波＋短反射 0.10')])
    gates='\n'.join(f'- {name}：{value}' for name,value in r['gates'].items())
    voice=json.loads((E/'final-voice/report.json').read_text(encoding='utf8'))
    quiet_wakes=sum(any(a['triggered'] for a in t['attempts']) for t in voice['trials'])
    quiet_complete=sum(t['complete'] for t in voice['trials'])
    normal=r.get('separate_normal_volume_functional_check')
    normal_note=''
    if normal:
        normal_report=json.loads((E/'normal-volume-voice/report.json').read_text(encoding='utf8'))
        normal_wakes=sum(any(a['triggered'] for a in t['attempts']) for t in normal_report['trials'])
        normal_complete=sum(t['complete'] for t in normal_report['trials'])
        normal_note=f"另做了一次数字增益 0.35 的独立功能核对：按顺序尝试 {normal['turns']} 轮，唤醒 {normal_wakes}/{normal['turns']}、完整问答 {normal_complete}/{normal['turns']}，连续对话状态 {'PASS' if normal['complete'] else 'FAIL'}，外部扬声器核对 {normal['external_speaker']}。这次高于压力测试的音量结果没有加入上述弱音成功率，也没有覆盖 0.10 链路的失败。"
    section=f'''## 2026-11-01 后续三轮：千问合成与低音量压力测试

**{status}** 已按用户要求改走合成音频路线，本轮未请求或采集真人读音。设备现为 R{r['selected_round']}，版本标识 `0.2.0-p4-wake4`，应用 SHA-256 `{r['app_sha256']}`。已启用语音、联网并恢复监听。

### 同一开发集的三轮比较

每轮都在前一候选权重上继续训练 4,000 步、导出三个 INT8 模型、做 C/Python/Torch 整数一致性检查、构建烧录并完成 36 次实际麦克风回放。保持 24/24/48 通道、阈值 740、输入增益 1、200 MHz。开发集含训练来源，以下用于选模，不能当作独立泛化成功率。

| 版本 | 普通话你好 | 普通话 Hi | 粤语你好 | 粤语 Hi | 反例误触发 |
|---|---:|---:|---:|---:|---:|
{comparison}

R1 加入实际低音量 P4 采音、普通话小言的拼音约束和弱音采样；R2 加入原生粤语、频响和短反射变化；R3 增加近似词及不完整词的负例变化和训练权重。选模使用事先固定的“正例宏平均召回与反例正确率各占一半”，同分依次比较最弱组、误触发数、较后轮次。选模没有使用下面的新保留录音。

### 最终独立录音回放

28 条新合成录音，每条进行三个条件，共 84 次。普通话使用未进入历次训练的新音色；粤语只做到录音独立，Rocky/Kiki 音色已在训练中。与 1,496 个既有/新增录音路径及训练来源检查没有重合。自动发音筛查在任何 KWS 测试之前完成，所有原始音频与排除理由保留。

| 发音 | 成功唤醒 / 回放 |
|---|---:|
{final}

反例触发：**{ratio(r['final_groups']['negative'])}**。这不是长期每小时误唤醒率。粤语近似人名的合成声调仍有歧义，被排除的录音不用于证明“小燕”等近似名字已经完全解决。

| 条件 | 普通话你好 | 普通话 Hi | 粤语你好 | 粤语 Hi | 反例误触发 |
|---|---:|---:|---:|---:|---:|
{conditions}

0.10 / 0.06 是主机数字增益，相对原来的 0.35 分别降低约 10.9 / 15.3 dB，不是实测声压。滤波条件模拟高频衰减和 23/53/95 ms 短反射；实际设备位置未移动。每组正例仅 2–4 条独立源录音，重复条件不是新说话人。**不能将上表等同于用户本人、真实方向或 20–50 厘米的成功率。**

### 原因与数据处理

原先源音频得分与 P4 实际采音得分不一致；本轮将普通外放降到 0.10 后，旧候选的另一组 16 次开发正例仅 1 次触发，8 次反例无触发。固定三条件开发集则是 4/24，反例 1/12。两个统计来自不同测试清单，不能混为一个成功率。实际采音与模型输入诊断显示，弱音和声道变化是必须补充的训练条件；无 DMA 丢帧，未把诊断模式下的抑制动作算成正常验收。

使用平台系统音色生成 144 条新录音，经过本地 ASR 与不提供目标文字的独立音频模型复核后，保留 48 条 TRAIN、28 条 TEST。另有两次接口探针。Qwen 3.1 的部分方言指令虽然被接口接受，实际输出仍被判断为普通话，因此整个 52 条粤语请求分组隔离；原生粤语输出也有短人名声调歧义，按内容一致性继续筛除。自动复核不是人工音素真值，也没有据此回改旧测试失败成绩。

本轮训练监测集仍来自 TRAIN，只用于优化与量化检查；最终测试全量完成，没有因为漏检或误触发而提前删减。本轮选模和拟合没有使用最终保留录音；这批数据之后只能算已知回归，继续优化须另外保留新测试。

### 资源与完整链路

- 唤醒模块运行堆：**{m['model_heap_bytes']:,} B（{m['model_heap_bytes']/1024:.2f} KiB）**。
- 其中推理工作区：**{m['workspace_bytes_included_in_heap']:,} B（{m['workspace_bytes_included_in_heap']/1024:.2f} KiB）**，已包含在上面的堆占用中，不能相加。
- 模型权重、描述与静音种子常量：**{m['model_constants_bytes']:,} B（{m['model_constants_bytes']/1024:.2f} KiB）**。Flash rodata 在本配置还会映射/复制到 PSRAM。四种发音共用这套模型，不是四倍占用。
- 本轮模型常量增量 {m['constant_delta_from_original_bytes']} B；应用 {m['app_bytes']:,} B，相对本轮开始前增量 {m['app_delta_from_this_turn_baseline']} B。
- 本批回放观测的单块最大推理耗时 {m['max_observed_inference_us']} µs。上述不是整机总内存，未把采音 DMA、线程栈及云对话缓存算成“唤醒词模型内存”。

{gates}

对话测试采用四种唤醒词依次进入真实 ASR→AI→扬声器流程、每轮最多一次唤醒尝试、外放数字增益 0.10。保存全部实际失败，不能用串口文字回复替代声学通过。三段 Flash 独立读回逐字节匹配，诊断模式关闭；最终联网、语音、监听及 30 秒空闲检查通过。短时空闲不能证明长期稳定性。

0.10 条件下：实际唤醒 {quiet_wakes}/{len(voice['trials'])}，完整问题与回答检查通过 {quiet_complete}/{len(voice['trials'])}。未唤醒、没有问题转写和转写不完整分别保留在每轮报告中，没有统称为成功对话。

{normal_note}

### 后续优化与交付

可以继续优化，但当前样本不足以计算可信的“最高成功率”，不能承诺 99% 或 100%。下一阶段可把具有代表性的使用条件中每组 ≥95% 作为目标，必须重新冻结新测试后验证。优先增加不同音色、弱音、真实声道变化和可靠的近似词数据，主要增加合成/筛查/训练成本，保持现有结构时无需增加设备模型内存。更大模型会增加 Flash、RAM、推理时间和功耗；直接降低阈值容易增加误唤醒。实际方位覆盖还需要真实摆位测试，滤波不能替代。

一个可量化的备选是将两路 24 通道初筛都扩大为 48 通道。根据当前数组和因果状态尺寸计算，预计另增约 6,016 B 工作区、31,984 B 模型与静音种子常量，神经网络乘加数约为当前 1.78 倍。它需要修改 P4 的固定结构运行时和种子格式；**未训练、未构建、未实测，不保证准确率提升**，实际堆分配、代码体积、耗时还需测量。推导保存在 `next-model-cost-estimate.json`，不计作本轮已实现的优化。

- 本轮证据：[result.json](evidence/wake4-three-rounds-20261101/result.json)，包含每轮结果、门槛、资源、读回哈希及最终条件。
- 当前候选可重刷包：[manifest.json](firmware/experimental-wake4-three-rounds-20261101/manifest.json)。若验收门槛为 FAIL，该包保持实验候选标记；原 `0.1.0-p4-3.5` 回退包保留。
- 本地千问 skill：`C:/Users/PC/.codex/skills/qianwen-model-suite`，已配置从用户提供的 `adev.key.txt` 读取凭据；压缩包为 `tools/skill_packages/qianwen-model-suite-1.1.0.zip`，51 项离线检查与实际请求通过。

以下为本轮之前的历史记录，其当时的“等待采样”“当前候选”等状态不代表本轮现状。

---

'''
    path=P/'WAKE4_REPORT.md';old=path.read_text(encoding='utf8')
    assert '## 2026-11-01 后续三轮：' not in old
    first,rest=old.split('\n',1)
    rest=rest.replace('## 当前结果','## 前一批 0.35/0.20 保留回放（历史）',1)
    path.write_text(first+'\n\n'+section+rest.lstrip(),encoding='utf8')
    path=P/'README.md';text=path.read_text(encoding='utf8');paragraph=text.split('\n\n')[1]
    text=text.replace(paragraph,f'按用户要求完成新一轮三次训练和低音量测试，当前设备为 R{r["selected_round"]} 的 `0.2.0-p4-wake4` 候选，已恢复联网与监听。{status} 本轮不采集真人读音。结果、内存和候选包见 [WAKE4_REPORT.md](WAKE4_REPORT.md)。以下 `0.1.0-p4-3.5` 是保留的适配基线，不是新增唤醒词的验收结论。',1)
    path.write_text(text,encoding='utf8')
    with (P/'ACTIONLOG.md').open('a',encoding='utf8') as f:
        f.write('\n\n## 2026-11-01：按用户要求完成另外三轮低音量优化\n\n'+status+'\n\n| 版本 | 普通话你好 | 普通话 Hi | 粤语你好 | 粤语 Hi | 反例误触发 |\n|---|---:|---:|---:|---:|---:|\n'+comparison+'\n\n'+f'最终选择 R{r["selected_round"]}，应用 `{r["app_sha256"]}`。84 次新录音条件回放、完整链路各项状态与资源以 `evidence/wake4-three-rounds-20261101/result.json` 为准。未请求真人采样；千问 skill 1.1.0 已安装打包，51 项离线检查通过。原有失败记录和回退固件不变。\n')
    with (P/'SPEC.md').open('a',encoding='utf8') as f:
        f.write('\n\n## 2026-11-01 后续三轮约束\n\n用户无法采集真人读音，授权使用提供的千问平台凭据和系统合成音色。本轮固定三次训练，低音量 0.10/0.06 及滤波反射条件，设备位置不变；按开发集预设规则选模，最终 28 条新录音、84 次条件回放仅测试一次。普通话音色独立，粤语仅录音独立。不得声称真实方位、距离或人群成功率，也不得把部分通过覆盖失败门槛。最新状态和完整指标见 WAKE4_REPORT.md 与 evidence/wake4-three-rounds-20261101。\n')
    print(json.dumps({'updated':['WAKE4_REPORT.md','README.md','ACTIONLOG.md','SPEC.md'],'status':r['status']},ensure_ascii=False))
if __name__=='__main__':main()
