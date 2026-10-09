> 2026-09-19 整理说明：旧录音、实验数据、日志、Flash 备份和旧二进制已列入用户要求的清理范围；自动删除被审批拦截，待执行根目录的清理入口。实际结果见 history/cleanup-result.json。清理后下文旧 artifacts 路径仅记录历史来源。完整源码在 [history](../history/README.md)，当前安装包在 firmware/latest；历史测试结论及未完成验收不变。

# Local PCM clip storage

All fields are little endian. The clip partition remains 448 KiB at 0x390000.
The format does not change the context partition or store cloud credentials.

| Header offset | Version 1 | Experimental version 2 |
| --- | --- | --- |
| 0 | Magic 0x31504341 | Same |
| 4 | 1 | 2 |
| 8 | Sample rate 16000 | Same |
| 12 | Retained PCM sample count | Same |
| 16 | CRC-32 of PCM bytes | CRC-32 of the entire encoded extent |
| 20 | Reserved | Encoded extent in bytes, excluding header |
| 24 | CRC-32 of bytes 0..23 | Same |
| 28 | Commit 0x54494d43 | Same |

The first 28 bytes are programmed before the four-byte commit. Missing commit,
bad header, invalid extent, unsupported version or bad data is rejected. CRC uses
reflected polynomial 0xedb88320, initial state 0xffffffff and final inversion.
The format does not claim transactional retention of the previous recording:
starting a new recording may erase it. Context has its separate dual-bank log.

Version 1 stores int16 PCM directly. Packed formats are enabled only by
`AGENT_PACKED_CLIP=ON`, which requires the background verifier. The writer now
uses version 3 for wake captures, and reads versions 1, 2 and 3. Manual recordings
retain version 1. The supported 0.6.0 build enables this option; legacy firmware rejects packed clips,
so rollback restores the prior version 1 clip.

Version 2 stores independent records of 1..128 samples. Header bits 0..6 contain
count minus one. Bit 7 selects raw PCM (0) or delta coding (1). A raw record holds
all samples as int16. A delta record starts with one literal int16, then:

- Tokens 0..126 add `(token - 63) * 16` to the preceding sample.
- Token 128 introduces a literal int16.
- Tokens 127 and 129..255 are invalid; int16 overflow is invalid.

Delta coding is used only when strictly smaller. Otherwise the raw record is
used. This is lossless for arbitrary int16 values, including clipping rails;
the microphone's multiples of 16 make common recordings compress well.

Version 3 retains the version 2 header layout and checksum rules, with version
field 3. Raw records remain identical. A delta record contains the count header,
one width byte (4, 6 or 8), then the first literal int16. Remaining values form
a bitstream, least significant bit first. For width `w`, all-ones is an escape
followed by a literal int16 in the same bitstream; other tokens add
`(token - ((1 << (w - 1)) - 1)) * 16`. The final byte has zero padding. Invalid
width, truncated literal, nonzero padding or int16 overflow is rejected.

Each record chooses the shortest of raw and the three delta widths. Raw wins
ties; smaller width wins delta ties. Selection changes neither the samples nor
their number. A bounded bit accumulator replaces per-byte delta decoding; page
buffering, publication, ownership and integrity checks remain the same. The
host checks include a previously committed version 2 clip, hand-built unaligned
negative literals, padding corruption with valid CRC and all existing faults.

One 256-byte page buffer aligns writes after the 32-byte header. Sectors are
erased on demand. Normal capture batches fit ten seconds even with raw fallback;
arbitrarily fragmented writes may exhaust storage and return `AGENT_ERR_FULL`.
Sample availability advances only when the complete encoded record is physically
stored. The producer publishes byte extent before sample count with release
ordering; the worker acquires both and reads only the published sample prefix.

One 256-byte reader cache belongs to the background worker during recording.
The owner joins that worker before verifying, replaying, exporting or reusing
the cache. A backward seek resets the sequential decoder; failed reads also
reset it so a retry cannot skip bytes. No index, extra heap allocation or PCM
expansion is required. Maximum retained duration remains ten seconds.

Before commit or trimming, the owner flushes the tail and verifies every
original input sample against the writer's PCM CRC, and every encoded byte
against the encoded CRC. Retained duration can end inside a record. The header
still covers the full encoded extent, including the discarded suffix, so its
corruption is not ignored. Reopening scans all records and validates the encoded
CRC, complete record boundaries, sample limit and retained prefix length.

Current validation: the baseline host suite includes packed clip fault tests.
Historical candidate datasets were removed during authorized cleanup; their
old measurements are not repeated acceptance claims. The complete accepted
same-device comparison remains in `artifacts/voice-ab-1000-v1/`.
