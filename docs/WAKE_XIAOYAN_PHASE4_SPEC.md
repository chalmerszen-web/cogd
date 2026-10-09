# Phase4: frozen-model physical acoustic test

Authorized 2026-09-21. Use Phase3 selected-model.json and threshold 920 unchanged.
PC Misiom-Shooter output -> ESP-HI microphone -> local trained C11 KWS.
Do not use PC microphone recognition as evidence of board detection.

1. Read live identity; preserve full verified Flash backup and old recording.
2. Build trained application; app <=1540096 bytes. Flash application only,
   verify non-application bytes before first boot. Keep 0.6.3 rollback.
3. Run Phase3 fixed-PCM board/host parity, then live microphone calibration.
   At most two playback-level adjustments on development sources only.
4. Freeze output/gain and selected cases before formal testing: 20 Mandarin,
   20 Cantonese, 20 negatives including partial words, confusables, old word,
   ordinary speech. Target each language >=18/20; negatives <=1/20 and no
   partial-word triggers. Count early triggers separately from valid hits.
   At most 10 diagnostic replays, excluded from formal scores.
5. Observe 30 minutes (ambient / known interference separately). Record actual
   inferred duration; uncontrolled room events are not proven false alarms.
   Require maximum block inference <32ms, p99 <=16ms, minimum heap >=32KiB,
   largest listening block >=24KiB, no DMA loss/reset or sustained heap decline.
6. Smoke-test USB, Wi-Fi, streaming/nonstreaming chat, light, LCD, music,
   recording, and context reboot recovery. Such tests may append context and
   replace recording; preserve prior recording in the full backup/export.
7. Deliver results, recordings, firmware hashes, rollback steps. On critical
   failure roll back; on recognition failure retain evidence and stop this run.

Budget: one formal pass; approximately 1.5-2 hours, no training or model/threshold
tuning. Replay is an acoustic-chain experiment, not human generalization proof.
