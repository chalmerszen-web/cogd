# P4 retained TTS playback bridge

`aoq_tts_play.c` owns no audio device or runtime kernel. The reserved runtime
worker supplies synchronous adapters to the existing single audio owner:

| Adapter | Existing P4 API |
| --- | --- |
| open | `esp_hi_stream_open(rate, ring, bytes)` |
| space | `esp_hi_stream_space()` |
| write | `esp_hi_stream_write(pcm, samples, current_job_cancel_flag)` |
| remaining | `esp_hi_stream_remaining()` |
| played | `esp_hi_stream_played()` |
| seal | `p4_media_audio_seal()` |
| finish | `esp_hi_stream_finish(error)` |
| cancelled / now_ms / wait_ms | Current job cancellation, monotonic clock and delay |

The caller must retain its network/audio reservation and adapter context for
the entire synchronous call. The helper does not turn voice off, change volume,
take a media lease, record a microphone, invoke the cloud, or call the DAC
directly. An audio open that returns BUSY is reported without stopping that
owner. The suggested runtime command is `tts play <exact generation>`; command
parsing and runtime/CMake integration are deliberately outside this helper.

The successful TTS cache is copied under its own existing generation guard into
at most 512 KiB of independent PSRAM. It cannot become a borrowed stale cache
pointer if a later trial replaces the original. A separate 12 KiB stream ring
is opened at mono PCM16 24 kHz, with writes at most 960 samples and no more than
the currently available ring space. `accepted_samples` describes the cache
copy; `written_samples` counts successful complete stream writes.

## Completion and cleanup contract

`audio_board.c` advances the ring read counter before `speaker_write`, so an
empty ring does not prove the last DMA samples have played. After writing,
the bridge seals EOF, waits for both ring empty and the existing `played`
estimate to reach the full count, then calls `finish` and checks counts again.
`played_samples` uses the existing P4 DAC-clock conversion capped by rendered
samples; neither this counter nor a PASS proves a person heard the speaker.

`finish` must be the real joining operation: its return promises the audio task
no longer borrows the ring, including on error. Source and ring are wiped/freed
only after that return. Cancellation, write error, or duration-plus-four-second
progress timeout also go through `finish(error)`. Cleanup is not abandoned at
the progress deadline: the existing owner may block while stopping hardware.
The report separates `playback_ms`, `join_ms`, `elapsed_ms` and `joined`.

The epoch cancellation entry is safe from another thread and does not carry a
request into a later explicit play. All playback operations are serialized;
reentrant/concurrent play attempts return BUSY. Adapter callbacks/context must
remain valid until return and must not replace the stream owner.

## Host verification

```sh
bash tests/aoq_tts_play/run.sh
```

The actual C bridge is compiled with C11, ASan/UBSan and `-Wall -Wextra -Werror`.
The cache, allocator, stream owner and monotonic clock are mocks. The 21 cases
cover the 512 KiB maximum, exact/wrong generation, BUSY and invalid cache sizes,
allocation/open/write failures, single-owner reentrancy, ordinary completion,
ring consumption leading DAC by 100 ms, cancellation before/during playback and
during join, stalled playback timeout, failed/short final counters and long
joining cleanup. The allocator asserts no source or ring is freed while the
fake audio owner is active. The tests do not establish real speaker output,
device timing, cancellation latency, audio quality or cloud correctness.

With the existing P4 compile database on Windows, compile this C file only:

```powershell
python tests/aoq_tts_play/compile_p4.py
```

This reuses the P4 runtime C compiler flags and writes only local evidence under
`.local/aoq-native-20261107/tts-play/`; it does not reconfigure, link or flash.
