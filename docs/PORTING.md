# Porting contract

The C11 core, JSON/SSE, tool policy, NOR log, merge and sync modules contain no ESP-IDF includes. `platform/port_contract.h` collects platform, transport, Flash, input/output and optional model-runtime contracts. `agent_plugin_t` provides the versioned registry and lifecycle.

M2 changes operation tables and uses ABI major 2. DIRECT LLM adapters implement `compose` with synchronous message/schema producers. Transports call `agent_http_write_body` before delivering any response bytes; it splits writes into at most 4096 bytes, checks cancellation and enforces the exact declared length. Never retain a producer's chunk after its write callback returns. Each HTTP attempt owns a fixed context selection until the request finishes; context writes/compaction reject that interval. Single records and response buffers remain 24 KiB while the bounded outgoing wire request may exceed that size.

ESP-IDF supplies time/random/KV, USB, one network worker/queue, Wi-Fi, verified HTTPS and NOR partition operations. Credential access is restricted to its HTTPS adapter. USB reserves GPIO18/19; the M0 board output initializes GPIO8 RMT after boot.

The optional audio extension adds `agent_audio_ops_t` from `plugins/audio/audio.h`: asynchronous score/capture/replay acceptance, stop, volume, microphone enable, status and typed inspection. A port supplies lazy DMA lifecycles, a separate durable clip store and fresh level timestamps. Portable score, meter, clip and replay DSP are reused unchanged. ESP-Hi's PDM DAC/ADC configuration and high-priority bounded worker are board choices, not assumptions imposed on other chips.

`agent_control_backend_t` supplies trusted pin capabilities, resource masks, GPIO/LED state access and managed audio operations. Reserve USB/Flash and every unverified pin before publishing capabilities; the model cannot enlarge this table. The board calls `agent_control_tick` regularly with monotonic milliseconds and shares one arbiter with direct tools. Keep claims during asynchronous cleanup until drivers stop and outputs are restored. A store that blocks DMA during erasure must coordinate admission with these resources.

Context recall takes portable `now_ms`/`clock_ctx` callbacks and a cancellation flag. Provide monotonic time so the ten-second scan bound is enforced between records. This clock is unrelated to synchronized wall time used for TLS validation. M3 operation tables use ABI 3, including typed song admission/readback; older tables must not be cast into the new shape. `agent_song_t` remains independent of SDK handles, contains 710 bytes and never expands into a complete PCM allocation. One local identity must have exactly one monotonic sequence allocator; gateways may only echo already-issued local IDs.

POSIX is the executable host verification target. `platform/posix/file_flash.c` models NOR with pread/pwrite, 1-to-0 checks, aligned erasure, fsync and restartable files. CTest also uses an in-memory NOR model for byte-level faults. Host tests run exactly the portable firmware modules under GCC C11, ASan and UBSan. This is not a claim that a desktop cloud agent has been shipped.

OpenWrt: provide monotonic time, entropy, durable KV sequence reservation, a verified HTTPS implementation, scheduling and a log-backed Flash adapter or equivalent durable contract. Board GPIO permissions and concrete distribution/TLS libraries must be selected for the target image. No OpenWrt binary is included.

JieLi: a specific SKU/SDK, memory map, toolchain and TLS/network capability are required. Implement the same bounded callbacks without exposing SDK handles in the kernel. No JieLi binary or simulated hardware success is claimed.

M4 uses ABI major 4: the typed audio table adds `listen`, and `agent_audio_state_t` includes listening ownership. Rebuild every plugin against these headers; older operation tables and state layouts are rejected through the descriptor ABI. Keyword inference, ADC/I2S and model allocation remain in the board adapter. The portable endpoint consumes 20-ms boolean speech decisions and bounds pauses, no-speech waiting and total recording time.

Model runtime is only `probe/load/infer/unload` with normalized events. Nothing registers or allocates it in M0. New tools must be statically described, parameter-validated and added to the allowlist; plugins cannot turn model text into shell commands.

The LCD extension uses ABI major 5. The typed tool table adds `hardware_prompt`, `hardware` and `display` operations. Supply board-specific facts; do not reuse ESP-HI wiring on other targets. Portable display parsing/time formatting/row rendering are in plugins/display; SPI, resource ownership and synchronized wall-clock acquisition are board responsibilities. A gateway must supply its own authoritative board prompt. Rebuild plugins; ABI 4 tables cannot be cast to ABI 5.
