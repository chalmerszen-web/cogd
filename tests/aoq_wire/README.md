# AOQ codec tests

These offline C++17 tests exercise the actual P4 codec sources. They check bounded partial decoding, malformed lengths/options, object cursors, PCM metadata and synthetic SDK-matched wire vectors. They require no network, credentials, device or SDK binary.

The stream-window test also covers out-of-order FIN, overlapping retransmissions, conflicting bytes/final sizes, overflow, invalid consumption and a long stream through a fixed-size buffer.

The track-profile test fixes three byte vectors produced independently by the
official SDK's pure profile builder and serializer: realtime Audio (388 bytes),
TTS without Audio publish (295 bytes), and ASR without Audio subscribe (327 bytes).
It checks exact bytes, audio directions and formats, preserved Data/command tracks,
and the legacy six-track wrapper. SDK provenance is recorded beside the vectors;
running the test does not load a vendor binary or read a private evidence file.

The video test adds independently serialized SDK vectors for JPEG Video 101,
dimensions/frame rate, LOC timestamp/key-frame/picture fields, and the subgroup
header. It rejects invalid dimensions/rates, malformed JPEG markers and a JPEG
whose SOF dimensions disagree with the negotiated profile. JPEG entropy decoding
and the camera driver remain outside this codec suite.

```
cmake -S tests/aoq_wire -B .local/aoq-wire-tests
cmake --build .local/aoq-wire-tests --config Release
ctest --test-dir .local/aoq-wire-tests -C Release --output-on-failure
```

Assertions remain enabled in Release builds. The vectors use fabricated tokens, JSON and PCM. Matching an SDK serializer does not establish cloud interoperability, audio correctness, track lifecycle or P4 performance; those require separate native hardware evidence.
