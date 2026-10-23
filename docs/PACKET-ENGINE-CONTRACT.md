# Offline Packet Engine Contract (Phase 2B.1)

## Scope and trust boundary

The `steamdpi_packet_lab` CMake target is an offline library. It has no Qt, WinDivert, Winsock, DNS, socket, or capture-controller dependency. Production capture code does not link to or call this target. Its inputs are bounded caller-owned byte buffers; this library has no API to obtain live packets or send packets anywhere.

Only synthetic fixtures and other explicitly authorized offline test data may be supplied. Never feed captured Steam sessions, credentials, authentication tokens, or full TLS application data into the lab. The fixture corpus in this phase is synthetic and contains only the ASCII marker `OFF` in payload bytes. The library does not persist inputs or payloads.

No packet transformation strategy that changes bytes exists in this phase. `PacketBuffer` exposes only const access. Mutation requires an explicit `mutableCopy()` which returns a separate, bounded `MutablePacketCopy`; changes cannot affect the original. Pipeline transforms receive `const PacketBuffer&`. If a transform reports mutation or returns a modified copy, the pipeline fails closed.

The lab does not infer process identity, Steam ownership, or flow identity. Any future flow-oriented operation must reject absent, contradictory, or ambiguous identity metadata; a hostname, port, or destination address alone is insufficient. Fragmented packets and unsupported IPv6 extension headers are rejected instead of guessed.

## Parsing rules

- Maximum stored packet size is 65,575 bytes. Oversized constructor input is rejected without retaining its bytes.
- IPv4 validates version, IHL, total length, header checksum, and rejects MF/fragment-offset packets. DF is allowed.
- IPv6 validates the fixed header and payload length. Jumbograms and every extension-header chain (including Fragment) are rejected in V1.
- TCP validates minimum/maximum data offset and available transport length, and verifies its pseudo-header checksum. Metadata includes flags, sequence and acknowledgment numbers.
- UDP validates its declared length against the IP payload and verifies its checksum. An omitted checksum is accepted only for IPv4; IPv6 requires a valid checksum.
- Unknown IP protocols, truncation, malformed lengths, and invalid checksums are errors. No parser reads outside the declared packet bounds.
- Trailing bytes after the declared IP packet length are ignored; packet metadata and checksums cover only the declared IP packet.

## Pipeline and replay bounds

The pipeline has a maximum of 16 transforms. Replay rejects more than 4,096 packets and examines at most 64 MiB per invocation. It retains no replay queue or payload history. `NoOpTransform`, `ValidationTransform`, and `DryRunTransform` are the only implementations. Replay metrics contain counts and byte totals only.

The lab must stay separate from WinDivert NETWORK-layer capture and reinjection. Future integration requires a separate reviewed design and is outside Phase 2B.1.
