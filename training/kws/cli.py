"""Small command dispatcher; all training/test operations are explicit."""
import argparse
import json
from pathlib import Path
import subprocess
import sys

DIRECTORY=Path(__file__).resolve().parent
sys.path.insert(0,str(DIRECTORY))

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('command',choices=['prepare','train','calibrate','evaluate'])
    args,remaining=parser.parse_known_args()
    if args.command!='prepare':
        raise SystemExit(subprocess.call([sys.executable,str(DIRECTORY/(args.command+'.py')),*remaining]))
    from data import read_manifest,prepare
    parser=argparse.ArgumentParser();parser.add_argument('--manifest',type=Path,required=True)
    parser.add_argument('--out',type=Path,required=True);parser.add_argument('--augmentations',type=int,default=3)
    args=parser.parse_args(remaining)
    print(json.dumps(prepare(read_manifest(args.manifest),args.out,args.augmentations),indent=2))

if __name__=='__main__':main()
