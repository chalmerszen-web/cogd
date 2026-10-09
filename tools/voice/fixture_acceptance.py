"""Narrow content checks for the fixed greeting/correction experiment.

These are fixture assertions, not a general semantic judge or listening test.
Protocol completion and acoustic measurements remain separate.
"""
import json
import re


def check_runtime_health(lines):
    """Serial diagnostic signatures, independent of successful tool content."""
    patterns = {
        'watchdog': r'^[EW] \(\d+\) (?:task_wdt|int_wdt): .*watchdog.*(?:triggered|timeout)',
        'panic': r'^Guru Meditation Error:|^abort\(\) was called',
        'reboot': r'^rst:0x[0-9a-fA-F]+|^ESP-ROM:esp32c3-',
    }
    counts = {name: sum(bool(re.search(pattern, line, re.IGNORECASE)) for line in lines)
              for name, pattern in patterns.items()}
    return dict(passed=not any(counts.values()), counts=counts)


def check_content(trial, fixture):
    events = trial.get('events', [])
    replies = [e.get('text', '') for e in events if e.get('stage') == 'fast_reply']
    replies += [line for line in trial.get('raw_lines', [])
                if line.strip() and not line.startswith(('@', '{'))]
    text = ''.join(replies)
    reasons = []
    rejected_tool_calls = 0
    if fixture == 'greeting':
        if not ('小言' in text and any(w in text for w in ('我是', '我叫', '助手', '设备', '設備'))):
            reasons.append('missing_requested_self_introduction')
        if any(w in text for w in ('临时', '草稿', '转写', '轉寫', '转录助手')):
            reasons.append('internal_role_leak')
        # This fixed Mandarin fixture does not request Cantonese. Catch the
        # observed UX107 language regression separately from name/role checks;
        # these explicit words are not a general acoustic language detector.
        prompt = ''.join(c for c in trial.get('prompt_text', '') if c.isalnum())
        if prompt == '请用一句话介绍你自己' and any(w in text for w in ('我係', '搞掂', '嘢', '喺', '唔')):
            reasons.append('unrequested_cantonese_in_mandarin_fixture')
    elif fixture == 'blue':
        # The fast local setter uses voice tool_done, not the full Agent's
        # @tool line. Require its named intent, successful result, readback
        # and completion speech; a query-only trace cannot qualify.
        names = [e.get('text') for e in events if e.get('stage') == 'fast_local_intent']
        results = []
        for e in events:
            if e.get('stage') == 'tool_done':
                try:
                    results.append(json.loads(e['text']))
                except (ValueError, TypeError, KeyError):
                    results.append(None)
        if names != ['device_light_set_rgb'] or results != [dict(ok=True,r=0,g=0,b=255)]:
            reasons.append('missing_or_wrong_local_blue_effect')
        if trial.get('light_readback') != dict(r=0,g=0,b=255):
            reasons.append('wrong_blue_readback')
        if not any(w in text for w in ('灯光设置好了', '灯光设定好喇')):
            reasons.append('missing_success_confirmation')
    elif fixture == 'correction':
        effects = []
        for line in trial.get('raw_lines', []):
            if line.startswith('@tool device_light_set_rgb '):
                try:
                    result = json.loads(line.split(' ', 2)[2])
                    if result.get('executed') is False and result.get('error'):
                        rejected_tool_calls += 1
                        continue  # Rejected validation is recorded, never an effect.
                    effects.append(result.get('ok') is True and
                                   result.get('r') == 0 and result.get('g', 0) > 0 and result.get('b') == 0)
                except (ValueError, TypeError):
                    effects.append(False)
        local_names = [e.get('text') for e in events if e.get('stage') == 'fast_local_intent']
        local_results = []
        for e in events:
            # Classic Agent also emits bare tool_done timing markers. Only
            # a payload is evidence of the fast local setter's result.
            if e.get('stage') == 'tool_done' and 'text' in e:
                try:
                    local_results.append(json.loads(e['text']))
                except (ValueError, TypeError, KeyError):
                    local_results.append(None)
        if local_names or local_results:
            # The final-only literal setter closes the old response and says
            # a generic completion. Prove green from both tool and readback;
            # never credit a query, stale blue effect or mixed execution path.
            if (local_names != ['device_light_set_rgb'] or effects or
                    local_results != [dict(ok=True,r=0,g=255,b=0)]):
                reasons.append('missing_or_wrong_local_green_effect')
            if trial.get('light_readback') != dict(r=0,g=255,b=0):
                reasons.append('wrong_green_readback')
            if not any(w in text for w in ('灯光设置好了', '灯光设定好喇')):
                reasons.append('missing_success_confirmation')
        else:
            if not effects or not all(effects):
                reasons.append('missing_or_wrong_green_effect')
            if not any(w in text for w in ('绿', '綠')):
                reasons.append('missing_green_confirmation')
        speculative = any(e.get('stage') == 'progress_transport' and e.get('text') == 'omni_manual_draft'
                          for e in events)
        for event in events:
            if event.get('stage') not in ('progress_generated', 'progress_start'):
                continue
            receipt = event.get('text', '')
            colours = ('红', '紅', '蓝', '藍', '黄', '黃', '青', '紫', '白', '黑')
            if speculative:
                colours += ('绿', '綠')
            if any(w in receipt for w in colours):
                reasons.append('parameter_specific_speculative_receipt' if speculative else 'wrong_colour_receipt')
                break
    else:
        raise ValueError('Unknown fixed content fixture')
    return dict(fixture=fixture, passed=not reasons, reasons=reasons, response_text=text,
                rejected_tool_calls=rejected_tool_calls,
                subjective_listening=False)
