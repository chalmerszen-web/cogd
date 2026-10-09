"""Pure, conservative evidence checks for the durable-memory voice fixture."""
import json


def memory_contains(value, fact):
    return (isinstance(value, dict) and value.get('present') is True and
            isinstance(value.get('through_event_id'), str) and bool(value['through_event_id']) and
            isinstance(value.get('text'), str) and bool(fact) and fact in value['text'])


def memory_effect(lines, fact, already_saved=False):
    """Track the last durable result; a verified existing fact needs no rewrite."""
    present = already_saved
    prefix = '@tool agent_context_summary_set '
    for line in lines:
        if not line.startswith(prefix):
            continue
        try:
            result = json.loads(line[len(prefix):])
        except ValueError:
            present = False
            continue
        if isinstance(result, dict) and result.get('executed') is False:
            continue
        present = memory_contains(result, fact)
    return present
