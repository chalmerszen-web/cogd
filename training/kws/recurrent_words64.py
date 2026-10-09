"""Host named9/event2 supervision projected to an explicitly tagged GRU64 kernel."""
from torch import nn
from recurrent64 import Recurrent64
from recurrent_words import WORD_CLASSES, TOKENS, joint_loss, source_targets


class RecurrentWordJoint64(Recurrent64):
    def __init__(self):
        super().__init__()
        self.output = nn.Linear(64, WORD_CLASSES + 2)


def event_projection(network):
    if type(network) is not RecurrentWordJoint64 or network.training:
        raise ValueError('Frozen named-word GRU64 required')
    state = {name: value.detach().clone() for name, value in network.state_dict().items()}
    state['output.weight'] = state['output.weight'][WORD_CLASSES:].clone()
    state['output.bias'] = state['output.bias'][WORD_CLASSES:].clone()
    output = network.output.weight
    projected = Recurrent64().to(device=output.device, dtype=output.dtype).eval()
    projected.load_state_dict(state, strict=True)
    return projected
