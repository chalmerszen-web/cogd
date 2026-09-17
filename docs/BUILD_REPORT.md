> 历史阶段记录：安装状态和产物路径描述当时的结果。2026-09-15 已采纳调优语音链路并清理旧中间材料；当前入口见 [BASELINE.md](BASELINE.md) 与 [CLEANUP_REPORT.md](CLEANUP_REPORT.md)。1000次正式证据完整保留，其数据不等于合并版的新1000次测试。

# Build report

Target: ESP32-C3 revision v0.4, 4 MiB Flash. Toolchain: project-private official ESP-IDF v6.1, commit `fff9895c82d744c7237be8847347bdd1b07c6643`. The preexisting IDF 5.5.1 installation was not changed. Host: WSL Ubuntu GCC 13.3, CMake/Ninja, C11, AddressSanitizer and UBSan.

Reference: xiaozhi-esp32 commit `ac6deed3d8e75348475364bf40ad953c6cd48054`, isolated under ignored `_ref/`. The canonical ESP-Hi reference build was attempted. The first attempt needed the existing Windows proxy for dependency downloads; the retried build reached about 2014/2108 compile steps, then Windows rejected an oversized generated command line. No reference source was patched or flashed.

The independent build uses native USB Serial/JTAG, Wi-Fi/networking, NVS, verified HTTP/TLS, raw partitions, timer and the pinned official LED strip component 3.0.3. M1/M2 add optional ADC/I2S drivers. Third-party cJSON 1.7.19 retains its MIT license. Project-owned sources compile as C11; no body/audio/LCD C++ application source is linked.

ESP-IDF 6.1 appends a default GNU C23 option. The component therefore applies a final per-source C11 option and asserts `__STDC_VERSION__ == 201112L`. SDK RISC-V inline headers require their `asm` spelling to be mapped to `__asm__` in hardware adapter units only; SDK source is unchanged. The portable modules compile with strict C11 and no such adapter macro on the host.

## Historical M0/M1 partition layout

| Name | Offset | Size |
|---|---:|---:|
| NVS | 0x9000 | 0x6000 |
| PHY | 0xF000 | 0x1000 |
| Factory app | 0x10000 | 0x290000 |
| Context | 0x2A0000 | 0x100000 |

This table is preserved as `docs/partitions-m1.csv`. M2's current layout is below. Both layouts were checked with the IDF partition generator; changing between them requires the verified migration, not an ordinary partition-table flash.

Original 4194304-byte backup: `artifacts/backups/esp32c3-original-4mb.bin`; SHA-256 `27689ECCB1E895413641FD35AA6A593D4545EBFD86F1C345D92C18531A009FAA`. Backup and logs may contain private state and are ignored by Git.

Reproducible command: `tools/idf.ps1 build size size-components`. M0 strict-C11 build and verified flashing are recorded in `artifacts/logs/release-c11-build.log` and `release-c11-flash.log`. Earlier phase logs retain their `phase-*` names. M0 numeric accounting remains in `artifacts/logs/size-summary.json` and `size-components.json`; its preserved source audit is `m0-source-audit.json`. Current audio evidence uses separate `audio-*` files.

## M0 0.1.0 size and identity

| Measurement | Bytes |
|---|---:|
| Flashable application binary | 928224 |
| IDF total image accounting | 927858 |
| Flash code | 697004 |
| Flash data | 146756 |
| Static DRAM allocation, including IRAM text | 182758 |
| Linker DRAM remainder | 138538 |
| Main component total / DRAM | 114643 / 80299 |
| RTC slow allocation | 60 |

The binary is about 907 KiB and fits the 0x290000-byte factory partition. Linker allocation and runtime free heap are different measurements; the latter is recorded in TEST_REPORT. Project-owned C implementation is 2181 lines across firmware and POSIX adapter sources, excluding tests and third-party code. All 14 owned firmware C compilation units passed the effective-C11 audit.

- `artifacts/releases/m0-0.1.0/esp_hi_agent.bin` SHA-256: `80BC09594418ED216D5EFCE294BEB9654C7D1FE9076D3BF9BA6F7A01876EEE35`.
- `artifacts/releases/m0-0.1.0/esp_hi_agent.elf` SHA-256: `8D27EBAA12FB8EB22B19D72F3187179112143AFAD2952963E3F7710DCF352B9F`.

The preserved M0 binary passed a fresh 100-turn hardware soak and a non-streaming four-tool continuation. Build artifacts, original backup, credentials and reference source remain outside Git through `.gitignore`.

## Audio 0.2.0 size and identity

The optional ADC/PDM extension remains C11, with 16 owned firmware compilation units. Build and verified COM5 flash: `audio-release-build.log`, `audio-release-flash.log`. No partition or NVS schema changed in M1. The preserved `artifacts/releases/m1-0.2.0/esp_hi_agent.bin` is 967136 bytes; SHA-256 `83E953C2307473E45C15937C0C0A9A461ACC39C65934187DC7ABA4839AE269FE`.

| Audio-enabled measurement | Bytes |
|---|---:|
| IDF total image accounting | 966768 |
| Flash code / data | 722940 / 158888 |
| Static DRAM including IRAM | 186756 |
| Linker DRAM remainder | 134540 |
| Binary with audio disabled | 928272 |
| Audio-enabled binary increment | 38864 |

The audio worker allocates 6144 stack bytes; measured remaining watermark is 5104. Twenty repeated real speaker/ADC lifecycle cycles show 0-byte change between the first and last five-sample free-heap medians. Concurrent music, microphone, DeepSeek TLS and LED tool use reached minimum heap 75352 bytes without reset. Exact size and runtime series are in `audio-size-summary.json`, `audio-size-components.json` and `audio-hardware.json`.

An independent `CONFIG_AGENT_AUDIO=n` target build and host build passed. The disabled ELF contains no project audio symbols (`audio-disabled-symbols.log`). SDK private dependencies are declared unconditionally because IDF expands dependencies before Kconfig; only enabled project audio sources reference the drivers. The SDK sources are untouched.

`tools/idf.ps1` now invokes the exported virtual environment's Python by absolute path, so repeated build/flash calls in the same PowerShell session do not accidentally use the base Python. Consecutive build and flash verified this correction.

## M2 current layout and resource design

| Name | Offset | Size |
|---|---:|---:|
| NVS | 0x9000 | 0x6000 |
| PHY | 0xf000 | 0x1000 |
| Factory app | 0x10000 | 0x180000 (1.5 MiB) |
| Context | 0x190000 | 0x200000 (two 1 MiB banks) |
| Local PCM clip | 0x390000 | 0x70000 (448 KiB) |

The migration and rollback prerequisites are in `M2_MIGRATION.md`. M2 app-only development flashes preserve the active context and clip; no chip erase or eFuse operation was used. The private 4 MiB backup taken after the initial M2 tests is `esp32c3-m2-before-final-20260911.bin`, SHA-256 `d4c3112921a41faa2c45c4e05e25a5cf0b72d45037a9a6789ec616bb0f74f14f`.

The engine occupies 60696 static bytes, context 11376, control executor 1440 and filtered replay state 2320. The context index/summary growth from M1 is 4640 bytes; there is no 64/128 KiB RAM request copy. The 4129-byte admitted USB work buffer is static. Task stacks are main 8192, network 16384, control 3072 and audio 6144 bytes. The network reservation was reduced after measuring over 7 KiB remaining on its 18 KiB setting, recovering heap for simultaneous TLS and DMA.

Exact final binary/map accounting and runtime minima are recorded in `m2-size-summary.json`, `m2-size-components.json` and TEST_REPORT. `CONFIG_AGENT_AUDIO=n` has an independent target and host build; its symbol audit checks score, voice, clip and board audio implementations are absent. The build stays on the pinned IDF source without SDK patches.

The final tested 0.3.0-dev application is **1011408 bytes**, with **561456 bytes** free in its app partition. SHA-256: `581a14820bc1ad53f5ff59564533c022c05c89a1b8eb1243ed53eec503b92d78`. This exact binary ran the successful 100-turn M2 soak. IDF image accounting is 1011046 bytes: Flash code 752522, Flash data 172476, DRAM including IRAM 201664, linker DRAM remainder 119632 and RTC allocation 60. The audio-disabled binary is 963472 bytes. Build/flash evidence: `m2-final-build.log`, `m2-final-flash.log`; source boundaries and effective C11 modes: `m2-source-audit.json`.

## M3 0.4.0-dev

Application: **1022320 bytes**, **550544 bytes** free in the unchanged app partition. SHA-256 `275fb26d8f82076671d72dc42d6add9813fa99ba3bc2837508bc9506d6942cfb`. Compared with M2, the complete music extension and durable local-range fix add 10912 image bytes; static DRAM decreases 504 bytes because upload reuses existing scratch and the USB line buffer is correctly bounded. No task stack or history budget was reduced in M3.

IDF image accounting: 1021948 bytes, Flash code 759336, Flash data 176564, DRAM including IRAM 201160, linker DRAM remainder 120136, RTC 60. Disabled application: 963616 bytes. Evidence: `m3-size-final.json`, `m3-components-final.json`, `m3-build-reboot-ranges.log`, `m3-build-noaudio-final.log` and `m3-disabled-symbols.json`. The disabled ELF contains no project song/synthesis/clip/replay/upload implementation symbols. The final C11 source audit has 30 owned compilation units and 5074 owned C lines, excluding tests and third-party code.

Runtime watermarks and physical readback identity are recorded separately in the music report; linker size is not a runtime free-heap measurement. The retained M2 application supports app-only rollback using the existing v1 context snapshot format.
