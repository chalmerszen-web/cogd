# TEN background confirmation in the accepted project baseline

The supported `tools/build_agent.ps1` enables this component, keyword checking,
32-kHz ADC acquisition and packed wake clips. Its CMake option defaults OFF for
ESP-IDF early dependency discovery; use the supported build wrapper to select
the complete profile. Audio-off builds exclude it.

The runtime is the exact coarse-profile-v1 speech candidate used as B in the
1000-trial same-board comparison. `sources.lock.cmake` hashes the selected
vendor, generated model, owned kernel and arithmetic sources. Stable copies
are in `third_party/ten-vad`, `third_party/rvfplib` and `kernel/`. The fixed,
fused-scale, linear-band, shared-pool and pitch implementations include the
accepted C3 optimizations; they are not unmodified upstream TEN.

All constructor allocations form one bounded group. The arena borrows the
engine workspace while its owner holds the turn reservation and work mutex.
Failed constructors release their allocations; inference does not allocate.
The worker stops before the owner releases the workspace. No context budget
or persistent index is reduced.

Only this archive uses renamed RISC-V float helpers. Agent/SDK arithmetic
continues resolving normally, without executable-wide symbol wrapping.
The C3 integration uses 1-kHz scheduling, a 6144-byte worker stack and the idle
keyword PCM buffers. Wall time, task CPU time and source sample time remain
separate measurements. Flash is read only up to the published sample/byte
boundary; join precedes trim, commit and cache reuse.

Current portable publication, phase, confirmation and clip tests are in
`host_tests/`. Older research harnesses were removed during authorized cleanup.
The accepted source archive and full A/B evidence remain in
`artifacts/baseline/reference/accepted-source.zip` and
`artifacts/voice-ab-1000-v1/`. See `docs/BASELINE.md` for integration checks and
`docs/VOICE_AB_1000_REPORT.md` for observed improvements, costs and limitations.
This project adoption does not claim that all historical M4 targets passed.
