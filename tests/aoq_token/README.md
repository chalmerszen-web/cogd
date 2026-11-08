# AOQ allocation host regression

Run on Linux or WSL with GCC/G++:

```sh
bash tests/aoq_token/run.sh
```

From the Windows checkout:

```powershell
wsl -d Ubuntu -- bash tests/aoq_token/run.sh
```

The runner compiles the **formal** `components/p4_aoq/aoq_token.cpp` from the
ESP32-P4 project by including it in the test translation unit. This makes its
private parser testable without a production hook or copied implementation.
It links the repository's cJSON, replacing only ESP-IDF HTTP, clock and
credential-provider APIs with local mocks. `sdkconfig.h` selects P4 solely for
this host build. No C3 or P4 production build settings are changed.

All 44 cases use the invented fixture in `fixtures/allocation.hpp` and in-memory
mutations. They cover raw/prefixed hex certificate pins, duplicate fields,
string and numeric bounds, embedded/escaped NULs, invalid endpoint syntax,
nesting and body limits, four model kinds (including Omni), rejected kinds/timeouts, HTTP status
mapping, chunked overflow, incomplete body, auth failure, timeout and open
failure. Expected output is `PASS 44 synthetic parser/HTTP/cleanup cases`.

The mock requires verified-bundle configuration, disabled redirects, the fixed
official HTTPS origin, POST `{}`, content type and the AOQ transport header.
It checks the owned Authorization header copy is overwritten with zeroes before
deletion and cleanup. Failed allocations and explicit allocation cleanup must
leave the output structure zeroed. The only auth value is an invented string;
the tests never read credentials, environment secrets, private allocation files,
serial ports or the network, and never print allocation fields. Automatic
stack/body/JSON wiping remains production RAII code; this suite does not claim
to prove the absence of every possible memory or log leak.

Assertions remain enabled. ASan/UBSan cover the run; the enum sanitizer alone is
disabled because one test intentionally passes an invalid C ABI kind value.
The temporary executable and cJSON object live under `/tmp` and are removed by
the runner. Only source, fixture and runner files belong in this directory.

These are synthetic host tests. They do not validate real ESP-IDF TLS/HTTP,
hard deadline cancellation, token service availability, QUIC or media delivery.
