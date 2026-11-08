# Native AOQ QUIC host regressions

These tests call the current vendored component in
`ESP32-P4-WIFI6-Touch-LCD-3.5/components/esp_http3`. They do not use the downloaded
`.local` audit tree or require ESP-IDF, a board, credentials, network access or an
AOQ allocation. No binaries or generated implementation snapshots are checked in.

Run from any directory on Linux or WSL:

```sh
bash /path/to/cogd/tests/aoq_quic/run.sh
```

From Windows PowerShell, when the repository is the current directory:

```powershell
wsl.exe -d Ubuntu -- bash ./tests/aoq_quic/run.sh
```

Prerequisites: Bash, Python 3, GCC with C++17/ASan/UBSan, and OpenSSL development
headers/library (`libssl-dev` on Ubuntu). `CXX` may select another compatible
compiler. Build outputs live in a unique temporary directory and are removed on
exit. A compile error, assertion failure, sanitizer diagnostic or failed case
produces a nonzero exit status; the script does not download dependencies.

## Coverage and implementation boundaries

- **Receive ACK deadline:** uses the actual ACK manager. An ACK-only packet
  updates the largest received number without postponing an already pending
  acknowledgment. Covers the 25 ms deadline, a zero-origin clock, reset after
  transmission, and immediate acknowledgment after two eliciting packets.
- **Transport parameters:** compiles the actual parser, frame, varint and type
  sources. Twenty boundary/atomicity checks cover invalid ACK exponents/delays,
  UDP payload and stream/CID limits, duplicate known/unknown parameters,
  truncated lengths, integer trailing bytes, oversized CID, reset-token length,
  unknown valid parameters and no partial output commit after failure.
- **Raw streams:** extracts the eight named current `QuicConnection::Impl`
  methods at runtime, with only their class qualifier changed for the fixture.
  Uses the actual flow controller. Checks raw byte preservation, bidi/uni IDs,
  FIN/send closure, wrong directions/unopened IDs, stream and receive-flow
  limits, offset overflow/final-size mismatch, duplicate/out-of-order frame
  delivery, consume limits and ambiguous send failure closing the connection.
  Congestion refusal must accept zero bytes without advancing offsets or
  closing a stream; FIN waits for send credit as well.
- **ALPN:** compiles the actual TLS ClientHello builders and parses their wire
  output. Checks default `h3`, custom ALPN, optional empty SNI, 255/256-byte
  boundaries, empty ALPN rejection, PSK encoding and non-PSK fallback.
- **Caller-supplied transport parameters:** the actual ClientHello and PSK
  builders preserve three AOQ-shaped synthetic parameter values byte for byte,
  including an embedded NUL case. Tests cover unchanged empty configuration,
  eight entries, 1024 total encoded bytes (ID, length and value), duplicate IDs,
  reserved core/standard overrides, non-varint IDs, aggregate overflow and
  rejection before handshake RNG/send. Values are synthetic, never allocation
  credentials. The existing 1500-byte Initial/output capacity still applies;
  maximal extras combined with large SNI/ALPN/PSK may safely fail to fit.
- **Certificate-pin policy:** extracts the actual certificate,
  CertificateVerify and Finished handlers. SHA256 is genuinely computed through
  host OpenSSL. Checks exact leaf pin, wrong pin, malformed pin size, CA fallback,
  verifier failure, mandatory signature validation before Finished, and bad
  Finished data rejection.
- **Routing CID prefix:** extracts the actual handshake, local CID generator
  and received-packet dispatcher. Tests unchanged empty-prefix RNG layout,
  1..4-byte prefixes on both initial CIDs, fresh random suffixes, invalid length
  rejection before send, and accepted Retry preserving the server CID verbatim,
  the original DCID, local SCID and token. The separate component path suite
  also checks locally advertised NEW_CONNECTION_ID prefix and invalid lengths.

## Explicit mocks and limits

The DATAGRAM regression extracts the current send/admission/size methods and
checks 33 payload-boundary and CID-size combinations, congestion refusal, failed
socket writes, ACK/loss accounting, and absence of a retransmittable frame copy.
It uses synthetic packet construction and does not prove live UDP delivery or
weak-network performance.

The ACK-credit regression extracts both production ACK entry points and uses
the real sent-packet tracker, loss detector and congestion controller. It covers
newly acknowledged in-flight bytes, duplicate/ACK-only/late ACKs, loss accounting,
and PTO for packets whose audio DATAGRAM/PING has no retransmittable frame copy.
Packet parsing and socket delivery are outside this fixture.

The fixture substitutes socket send/close, ESP memory allocation/logging,
certificate DER parsing/usage/chain validation, signature verification and
key-derivation/Finished cryptographic primitives. PSK binder primitives return
fixed values to exercise **encoding**, not cryptographic correctness. The
certificate bytes are public synthetic test data, not a server certificate or
credential. A mocked signature verifier is made to both accept and reject so
the production authentication policy gates are tested.
The CID fixture additionally mocks RNG, key exchange, socket sends and Retry
integrity parsing; its accepted/rejected Retry outcomes test the real dispatcher
policy, not cryptographic verification of a Retry packet.

The tests therefore do **not** establish real certificate-chain verification,
TLS key exchange/signature correctness, full `StartHandshake`/pin-configuration
admission, negotiated ALPN rejection on a live peer, H3 initialization exclusion
at runtime, QUIC packet encryption/retransmission, ordered application stream
reassembly, AOQ wire/media interoperability, audio/video quality or P4 hardware
behavior. Keep separate P4 compilation, native handshake and bounded media
acceptance evidence. These tests must never justify enabling an unverified-peer
override.
