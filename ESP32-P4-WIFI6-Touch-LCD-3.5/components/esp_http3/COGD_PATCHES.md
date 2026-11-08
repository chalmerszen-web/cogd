# COGD native AOQ experiment

Upstream: [78/esp-http3 1.6.3](https://components.espressif.com/components/78/esp-http3/versions/1.6.3/readme), Apache-2.0; see `LICENSE`.
This is a third-party component published by `78`, not an Espressif AOQ implementation.
Upstream archive SHA-256: `6cd7514ced273a3d97ef0c4300a099a69442850ec61c9a71083afe17d23d07b3`.

Local modifications:

- Add explicit raw QUIC mode and configurable ALPN without HTTP/3 streams or framing.
- Add an optional client CID routing prefix, limited to four bytes of each eight-byte locally generated CID. The remainder is random. Server-provided CIDs and Retry handling retain their original values. An empty prefix preserves upstream behavior.
- Add optional opaque transport-parameter extensions, limited to eight entries and 1024 encoded bytes. Duplicate IDs, known standard IDs and varint overflow are rejected. Existing output-buffer limits still apply to the complete ClientHello. The QUIC core contains no hardcoded AOQ parameter IDs or session values.
- Add certificate SHA-256 pin verification for relay certificates obtained through authenticated HTTPS allocation. CertificateVerify and Finished remain required; pin mode disables PSK resumption and cannot enable unverified peers.
- Expose bounded raw stream writes and offset-bearing receive callbacks. Applications must reassemble and deduplicate received bytes.
- Reject malformed, duplicate, oversized and out-of-range transport parameters atomically.
- Avoid a zero-length `memset` with a null pointer in packet construction.
- Bound DATAGRAM payloads against the peer's full-width frame limit, local frame limit, packet budget, CID size and AEAD overhead. Count successfully sent DATAGRAM packets in ACK/loss/congestion accounting without retaining their frames for retransmission; failed socket writes are not recorded as sent.
- Return newly acknowledged in-flight bytes to congestion control in both ACK entry paths. ACK-only packets and duplicate ACKs cannot create credit. Keep PTO active for pending DATAGRAM/PING packets even though they have no retransmittable frame copy.
- Gate raw STREAM writes and FIN on conservative congestion space for a 1200-byte packet. A blocked write accepts zero bytes; callers poll ACKs and retry. `CanSendRawPacket()` lets the video sender defer FIN without classifying backpressure as a transport failure.
- Make the retained raw stream-record limit explicit (1..512, default 64). The bounded conversation profile selects 512 for up to 180 video frames, 256 peer streams, and control/Data streams. Closed records retain final sizes for late-duplicate validation; they are not erased to manufacture capacity. Peer stream credit and application receive-window limits remain separate.

The default HTTP/3 mode is retained. COGD only selects this component from the P4 experimental build. C3 does not require it.

This remains an interoperability experiment. The raw connection retains a bounded number of stream records per lifetime; its packet-order callbacks are not an ordered stream API. The inherited QUIC/TLS implementation supports a limited set of handshake algorithms, and congestion, path MTU, reset/final-size handling and long sessions need further validation before a production release. Successful compilation or a setup handshake alone does not establish working speech or video.
