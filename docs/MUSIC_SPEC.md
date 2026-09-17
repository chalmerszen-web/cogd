# M3 expressive music

0.6.2 correction: a complete invalid song now returns bounded diagnostic tool results (including pattern tick totals and valid reference range) to the model before any call in that batch executes. At most two correction responses are allowed within the existing four tool rounds. Original call IDs are preserved; truncated replies, unknown tools, duplicate IDs and failures after executed calls are not automatically repaired. This does not raise the 4 KiB argument or 75-second limits and is not a new subjective music acceptance.

Status: IMPLEMENTED AND OBJECTIVELY VALIDATED on COM5, 0.4.0-dev. Requirements M3-01..08 have measured evidence in `MUSIC_REPORT.md` and the release manifest. The 100 valid network rounds are explicitly segmented, not an uninterrupted run. Subjective listening remains PENDING; M3 is not marked as a completed music-experience acceptance. M0 external blockers remain unchanged.

| ID | Requirement | Acceptance |
|---|---|---|
| M3-01 | Four fixed voices: electric keys lead, bass, plucked arpeggio, drums; per-note velocity and gate | Deterministic C11 renderer; measured dynamics and decay |
| M3-02 | New `device.audio.play_song` / `device_audio_play_song`, bounded asynchronous admission | Legacy score compatibility; validate entire tool batch before effects |
| M3-03 | Pattern library: 16 patterns, 128 stored notes, 32 sequence segments, 75-second ceiling | Invalid/overflow inputs reject without changing active/last song |
| M3-04 | USB transactional upload/readback, <=4 KiB; ordinary chat <=2 KiB | CRC, offset, timeout, cancellation and busy regressions |
| M3-05 | Real device-to-DeepSeek creation: 4/4, 128 BPM, 32 bars, ~60s | At least three distinct accepted candidates; stream and nonstream |
| M3-06 | Local acoustic evidence via Misiom-Shooter, 44100 Hz mono PCM16 | Raw recordings, matched C-rendered WAV, noise/level/timing analysis |
| M3-07 | Stable music + ADC + LED + network operation | 100 real turns; >=48 KiB minimum heap; no warm heap decline, DMA loss or reset; render 240 samples <3ms; cancel <=500ms |
| M3-08 | Release evidence and rollback; existing data untouched | Same partition layout; before-Flash backup/hash, verified app and reports |

## Wire contract

New song version 1 uses `{"v":1,"bpm":128,"patterns":[[[60,4,100,85]]],"sequence":[[0,-1,-1,-1,100]]}`.
Every pattern spans 32 sixteenth-note ticks (two 4/4 bars); trailing time is silence. Each note is `[pitch,ticks,velocity,gate]`, pitch 0 (rest) or MIDI 36..96, ticks 1..32, velocity 1..127, gate 1..100 percent of its step. The sum of pattern ticks cannot exceed 32. Across the pattern library at most 128 notes; a pattern must contain at least one note. BPM 60..180. Sequence rows are `[lead,bass,arpeggio,drums,gain]`; references are -1 for silence or zero-based pattern indices, gain 1..127. Drum-referenced patterns may only use 0,36 (kick),38 (snare),42 (closed hi-hat). One renderer per voice; a new note releases/replaces the prior note without accumulating voices. The total sequence duration, including padded time, must be <=75 seconds. Unknown/duplicate keys, fractional integers, invalid references and malformed JSON reject. Serialization <=4096 bytes, and the existing combined tool-argument bound still applies.

Old `play_score`, 30-second scores and 60-second control plans remain separate. Music owns the same speaker resources and is stopped by existing cancellation. The entire song is compiled into a fixed object, not a PCM buffer or expanded repeated notes. Admission accepts one job; resource release follows actual worker shutdown. Song readback is canonical JSON plus CRC/job identity.

Rest notes may use zero velocity/gate; pitched notes still require positive values. A song with no referenced pitched note is rejected. The compact song object measures 710 bytes, with a 208-byte renderer state; the board keeps two song slots for transactional replacement. Added typed audio operations bump the plugin ABI to major 3.

Song playback primes the output with 300 ms of digital silence before musical sample zero. Matched acoustic tests found about 150 ms of first-note attenuation without priming; priming restores the complete attack. The musical sample count excludes priming and the 40 ms drain. The USB draft diagnostic reports failed generated arguments and parser status, never executes them.

USB transport: `agent audio song begin BYTES CRC32HEX`, then contiguous `agent audio song chunk OFFSET HEX` blocks (<=256 decoded bytes), then `agent audio song commit`. The absolute upload timeout is 15 seconds; `agent audio song abort` and `agent cancel` discard uncommitted data. `agent audio song get` returns canonical JSON plus `@song LENGTH CRC32HEX`; use audio status `last_song_job` to identify the accepted song. `tools/music_link.py SONG.json` implements this protocol. Readback is volatile across reboot, while successful dialogue/tool arguments remain in the persistent context; saved host JSON can be uploaded again.

## Measurement and delivery

Record only during announced test windows, without normalization, with silent pre/post-roll. Start below master 80 and never exceed 80 during tuning. Use the same C synthesis for host reference and firmware; compare calibration excerpts before selecting three full candidates. Target musical structure: 4 intro bars, 8 theme, 8 theme variation, 8 contrasting bars, 4 ending bars. Preserve failed candidates and measurements. The final selection plays completely three times. Microphone placement and OS gain must remain stable for relative comparisons. No recording upload, transcription or calibrated SPL claim. Current tools provide objective signal analysis, not direct model listening; subjective acceptance remains explicitly separate.

All generated evidence and recordings stay under ignored `artifacts/`. Preserve the M2 release and all real context/clip/NVS data; flash only the application image. Update SPEC, ACTIONLOG, music/audio docs and release manifest as checks complete.
