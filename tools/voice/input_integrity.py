"""Conservative fixture transcription check, separate from task/tool success."""
import unicodedata


def complete_input(expected, actual):
    # Ignore punctuation, spacing, Latin case and compatibility width only.
    # Paraphrases are not silently accepted as evidence of retaining all words.
    def normalized(text):
        return ''.join(c for c in unicodedata.normalize('NFKC', text).casefold() if c.isalnum())
    expected = normalized(expected)
    return bool(expected) and expected == normalized(actual)
