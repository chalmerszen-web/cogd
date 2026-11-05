"""Function-preserving 48 -> 96 widening before explicit symmetry breaking."""
import argparse,hashlib,json,sys
from pathlib import Path
import torch
sys.path.insert(0,str(Path(__file__).resolve().parent/'kws96_training'))
from model import Model
def main():
    p=argparse.ArgumentParser();p.add_argument('--initial',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args();a.out.mkdir(parents=True,exist_ok=False)
    torch.set_num_threads(4);torch.manual_seed(11019696);report=[]
    for name in ('first','second','verifier'):
        source=a.initial/name/'best.pt';saved=torch.load(source,map_location='cpu',weights_only=True);old=Model(48).eval();old.load_state_dict(saved['state_dict']);width=48 if name=='verifier' else 96;new=Model(width).eval();state={}
        if width==48:state=saved['state_dict']
        else:
            for key,value in saved['state_dict'].items():
                if key.startswith('layers.') and key.endswith('weight'):
                    layer=int(key.split('.')[1]);v=value
                    if layer!=11:v=v.repeat_interleave(2,0)
                    if layer and (not layer%2 or layer==11):v=v.repeat_interleave(2,1)/2
                    state[key]=v
                elif key.startswith('norms.') and value.ndim:state[key]=value.repeat_interleave(2)
                else:state[key]=value.clone()
        new.load_state_dict(state);x=torch.randn(2,40,384)
        with torch.no_grad():
            expected=old(x);actual=new(x);error=float((expected-actual).abs().max());torch.testing.assert_close(expected,actual,rtol=1e-4,atol=5e-5)
            if width==96:
                for key,value in state.items():
                    if key.startswith('layers.') and key.endswith('weight'):value.add_(torch.randn_like(value)*float(value.std())*.001)
        saved.update(state_dict=state,config=dict(saved['config'],channels=width,expansion_parent_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),expansion='Net2Wider x2 then0.001 std symmetry-breaking perturbation'))
        folder=a.out/name;folder.mkdir();torch.save(saved,folder/'best.pt');report.append({'head':name,'channels':width,'frames':768,'max_abs_error_before_perturbation':error})
    (a.out/'widening-parity.json').write_text(json.dumps(report,indent=2),encoding='utf8');print(json.dumps(report))
if __name__=='__main__':main()
