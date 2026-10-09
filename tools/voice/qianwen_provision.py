"""Provision the explicitly requested skill Key into independent device NVS."""
import argparse
import json
from pathlib import Path
from device import Device
from qianwen_credentials import qianwen_key


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--out',required=True,type=Path)
    p.add_argument('--enable',action='store_true');a=p.parse_args()
    key=qianwen_key();d=Device(a.out)
    try:
        state=d.command('agent status',query=True)
        assert state['version']=='0.9.0-qwen-stream'
        d.command('agent voice off')
        d.command('agent voice configure '+json.dumps({'provider':'qianwen','api_key':key,'voice':'longanhuan_v3.6'}),secret=True)
        if a.enable:d.command('agent voice on')
        voice=d.command('agent voice status',query=True)
        context=d.command('agent context stats',query=True)
        assert voice['provider']=='qianwen' and voice['configured'] and context['history_budget']==204800
        print(json.dumps({'voice':voice,'history_budget':context['history_budget']},ensure_ascii=False))
    finally:d.save();d.close()


if __name__=='__main__':main()
