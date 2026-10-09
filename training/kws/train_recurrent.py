"""Bounded host-only GRU event training; no deployment or data-set selection.

The full immutable TRAIN inputs and audited time supervision remain intact.
Parameter projection constrains training to the explicitly selected matrix ABI; it
does not replace later integer acoustic/event parity or independent validation.
"""
import time
import numpy as np
import torch
from recurrent import Recurrent32
from recurrent64 import Recurrent64
from train_ctc import load_inputs,sampling_pools,draw_indices,save_json,sha256
from word_event import event_targets,event_loss,TOKENS
from ctc_timed import audited_final_windows
from optimizer_schedule import cosine_rate,declared_schedule


def project_parameters_(network,matrix_q=8,*,contract=None):
    """Projected optimization, never a silently clamped integer export.

    Check every tensor before writing any, then constrain matrices to INT8 Q8
    or explicit Q6 and biases to INT16 Q8. Default preserves the original ABI.
    """
    if type(matrix_q) is not int or matrix_q not in (6,8):
        raise ValueError('Unsupported matrix scaling')
    if not isinstance(network,(Recurrent32,Recurrent64)):
        raise ValueError('Expected a fixed GRU32 or GRU64 model')
    if isinstance(network,Recurrent64):
        from recurrent64 import CONTRACT as contract64
        from recurrent64_q6 import CONTRACT as contract64_q6
        if matrix_q==6 and contract!=contract64_q6:
            raise ValueError('Explicit tagged GRU64 Q6 deployment contract required')
        if matrix_q==8 and contract not in (None,contract64):
            raise ValueError('GRU64 Q8 deployment contract mismatch')
    parameters=tuple(network.parameters())
    if not all(bool(torch.isfinite(parameter).all()) for parameter in parameters):
        raise ValueError('Nonfinite trainable parameter')
    changed=0
    with torch.no_grad():
        for parameter in parameters:
            scale=1<<matrix_q
            low,high=(-128/scale,127/scale) if parameter.ndim==2 else (-128.,32767/256)
            changed+=int(((parameter<low)|(parameter>high)).sum())
            parameter.clamp_(low,high)
    return changed


def fit(root,out,plan):
    """One fresh fit, terminal checkpoint only; CPU and wall time are frozen."""
    began=time.monotonic()
    data,labels,blank=load_inputs(root,plan)
    cfg=plan['model']
    width=cfg.get('hidden',32)
    if type(width) is not int or width not in (32,64):
        raise ValueError('Unregistered recurrent width')
    matrix_q=cfg.get('matrix_q',8)
    if type(matrix_q) is not int or matrix_q not in (6,8):
        raise ValueError('Unsupported matrix scaling')
    if matrix_q==6:
        if width==64:
            from recurrent64_q6 import CONTRACT
        else:
            from recurrent_q6 import CONTRACT
        if plan['runtime_contract']!=CONTRACT:
            raise ValueError('Explicit Q6 deployment contract required')
    schedule=declared_schedule(cfg,plan)
    torch.set_num_threads(cfg['CPU_threads'])
    torch.manual_seed(cfg['seed'])
    torch.use_deterministic_algorithms(True)
    if torch.__version__!='2.6.0+cpu':
        raise ValueError('Frozen local PyTorch2.6 CPU environment required')
    phonetic=cfg.get('phonetic_regularizer',False)
    named=cfg.get('named_word_regularizer',False)
    if phonetic and named:
        raise ValueError('Exactly one auxiliary supervision contract required')
    joint=phonetic or named
    if width==64:
        if matrix_q==6:
            from recurrent64_q6 import CONTRACT as contract64
        else:
            from recurrent64 import CONTRACT as contract64
        if not named or phonetic or plan['runtime_contract']!=contract64:
            raise ValueError('Explicit named9/event2 GRU64 deployment contract required')
        from recurrent_words64 import RecurrentWordJoint64 as JointNetwork
        from recurrent_words64 import joint_loss,source_targets,TOKENS as phone_tokens
    elif named:
        from recurrent_words import RecurrentWordJoint32 as JointNetwork
        from recurrent_words import joint_loss,source_targets,TOKENS as phone_tokens
    elif phonetic:
        from recurrent_joint import RecurrentJoint32
        from joint_event import joint_loss
        from ctc_data import TOKENS as phone_tokens
        JointNetwork=RecurrentJoint32
    network=(JointNetwork() if joint else Recurrent32()).train()
    expected_parameters=21067 if width==64 else 7467 if named else 7632 if phonetic else 7170
    assert sum(p.numel() for p in network.parameters())==cfg['parameters']==expected_parameters
    projected=project_parameters_(network,matrix_q,contract=plan['runtime_contract'])
    optimizer=torch.optim.Adam(network.parameters(),lr=cfg['learning_rate'])
    pools=sampling_pools(data,labels)
    rng=np.random.default_rng(cfg['seed'])
    features=torch.from_numpy(data['x'].astype(np.float32).transpose(0,2,1)/32)
    raw_targets,raw_lengths=event_targets(data['label'].astype(np.int64))
    targets=torch.from_numpy(raw_targets);lengths=torch.from_numpy(raw_lengths)
    quiet=torch.from_numpy(blank.astype(np.float32).T[None]/32)
    prefix=quiet[:,:,:cfg['known_blank_frames']]
    quiet_batch=quiet.expand(cfg['blank_rows'],-1,-1)
    quiet_targets=torch.zeros((cfg['blank_rows'],1),dtype=torch.int64)
    quiet_lengths=torch.zeros(cfg['blank_rows'],dtype=torch.int64)
    windows=torch.from_numpy(audited_final_windows(data['event_start_accept'],
        data['event_end'],data['label'].astype(bool)))
    quiet_windows=torch.full((cfg['blank_rows'],2),-1,dtype=torch.int64)
    if joint:
        if named:
            raw_words,word_lengths=source_targets(labels['class_id'],data['label'].astype(bool))
            phone_targets=torch.from_numpy(raw_words)
            phone_lengths=torch.from_numpy(word_lengths)
        else:
            phone_targets=torch.from_numpy(labels['targets'])
            phone_lengths=torch.from_numpy(labels['lengths'])
        quiet_phones=torch.zeros((cfg['blank_rows'],phone_targets.shape[1]),dtype=torch.int64)
    visits=np.zeros(len(features),np.int32)
    history=[];first=None;fitting=time.monotonic()
    try:
        for step in range(1,cfg['steps']+1):
            if time.monotonic()-fitting>plan['maximum_fit_seconds']:
                raise TimeoutError('Frozen GRU fit budget exhausted; no restart')
            if schedule is not None:
                rate=cosine_rate(*schedule,step)
                for group in optimizer.param_groups:group['lr']=rate
            indices=draw_indices(pools,rng)
            np.add.at(visits,indices,1)
            suffix=torch.cat((features[indices],quiet_batch),0)
            values=torch.cat((prefix.expand(len(suffix),-1,-1),suffix),-1)
            wanted=torch.cat((targets[indices],quiet_targets),0)
            sizes=torch.cat((lengths[indices],quiet_lengths),0)
            intervals=torch.cat((windows[indices],quiet_windows),0)
            optimizer.zero_grad(set_to_none=True)
            logits,_=network(values)
            if joint:
                phones=torch.cat((phone_targets[indices],quiet_phones),0)
                phone_sizes=torch.cat((phone_lengths[indices],quiet_lengths),0)
                objective=joint_loss(logits,phones,phone_sizes,wanted,sizes,intervals,cfg['known_blank_frames'])
            else:
                objective=event_loss(logits,wanted,sizes,intervals,cfg['known_blank_frames'])
            if not bool(torch.isfinite(objective)):
                raise ValueError('Nonfinite whole-event objective')
            objective.backward()
            norm=torch.nn.utils.clip_grad_norm_(network.parameters(),cfg['gradient_clip'],error_if_nonfinite=True)
            optimizer.step()
            projected+=project_parameters_(network,matrix_q,contract=plan['runtime_contract'])
            if first is None:first=float(objective.detach())
            if step%100==0 or step==cfg['steps']:
                row=dict(step=step,loss=float(objective.detach()),gradient_norm=float(norm),
                    cumulative_projected_values=projected,seconds=round(time.monotonic()-fitting,3))
                if schedule is not None:row['learning_rate']=rate
                history.append(row);save_json(out/'history.json',history)
                print(__import__('json').dumps(row),flush=True)
        network.eval()
        torch.save(dict(state_dict=network.state_dict(),step=cfg['steps'],
            config=dict(**cfg,tokens=list(TOKENS),auxiliary_tokens=list(phone_tokens) if joint else [],objective=plan['objective'],
                runtime_contract=plan['runtime_contract'],candidate='only-final',input_hashes=plan['inputs'])),
            out/'checkpoint.pt')
        report=dict(complete=True,fits=1,final_step=cfg['steps'],parameters=expected_parameters,
            first_loss=first,final_batch_loss=history[-1]['loss'],fit_seconds=round(time.monotonic()-fitting,3),
            total_seconds=round(time.monotonic()-began,3),rows_seen=int((visits>0).sum()),
            total_rows=len(visits),optimizer_draws=int(visits.sum()),cumulative_projected_values=projected,
            checkpoint_sha256=sha256(out/'checkpoint.pt'),TRAIN_only=True,calibrations=0,exports=0,
            quality_passed=None,false_wake_fixed=False)
        save_json(out/'fit-report.json',report)
        return network,data,labels,blank,report
    finally:
        np.save(out/'visit-counts.npy',visits)
