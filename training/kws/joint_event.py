"""Two supervised views of one encoder; phonetic head is host-training only.

This is a fresh model, never a refit of a rejected acoustic checkpoint.
Whole-event and audited-source token objectives have equal normalized weight.
Deployment retains exactly the original2 event outputs and shared backbone.
"""
import torch
from model import Model,Causal
from word_event import WordEventModel,event_loss
from ctc_timed import timed_sequence_loss
from ctc_data import TOKENS

CONTRACT='xiaoyan_joint_ctc14_event2_training_only_v1'
PHONE_CLASSES=len(TOKENS)


class JointEventModel(Model):
    def __init__(self):
        super().__init__(48)
        self.layers[-1]=Causal(48,PHONE_CLASSES+2,1,bias=True)


def joint_loss(logits,phone_targets,phone_lengths,events,event_lengths,windows,
               known_blank_frames=128):
    if logits.ndim!=3 or logits.shape[1]!=PHONE_CLASSES+2:
        raise ValueError('Expected separate14-token and2-event heads')
    phones=timed_sequence_loss(logits[:,:PHONE_CLASSES],phone_targets,
                              phone_lengths,windows,known_blank_frames)
    word=event_loss(logits[:,PHONE_CLASSES:],events,event_lengths,
                    windows,known_blank_frames)
    return (phones+word)/2


def event_projection(network):
    """Copy an exact event-only model; no averaging, threshold, or retraining."""
    if not isinstance(network,JointEventModel) or network.training:
        raise ValueError('Projection requires a frozen exact joint model')
    state=network.state_dict()
    state['layers.11.conv.weight']=state['layers.11.conv.weight'][PHONE_CLASSES:].clone()
    state['layers.11.conv.bias']=state['layers.11.conv.bias'][PHONE_CLASSES:].clone()
    result=WordEventModel().eval()
    result.load_state_dict(state,strict=True)
    return result
