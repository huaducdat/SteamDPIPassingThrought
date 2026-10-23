# SteamDPI Phase 2B.1 — Offline Packet Laboratory

**STATUS: SUCCESS** — The offline packet lab, deterministic fixtures, tests, performance measurement, and existing GUI/regression smoke checks completed. No live packet interception or modification was introduced.

## Implemented

- Added `steamdpi_packet_lab`, a standard-library-only C++20 target separated from Qt and WinDivert.
- Added bounded `PacketBuffer`, explicit mutable-copy workflow, IPv4/IPv6 TCP/UDP parser, validator, metadata, transformation pipeline, fixture loader, replay runner, and aggregate metrics.
- Implemented only `NoOpTransform`, `ValidationTransform`, and `DryRunTransform`. Pipeline rejects any transform that claims to modify or return a modified packet.
- Deterministic synthetic fixtures cover IPv4 TCP, IPv6 TCP, IPv4 UDP, invalid IPv4 IHL, invalid TCP data offset, truncation, invalid IP packet length, invalid UDP length, invalid checksums, IPv4 fragmentation, IPv6 extension-header rejection, and seeded random malformed buffers.
- Added a standalone offline benchmark executable.

Detailed invariants and parser rules are documented in [PACKET-ENGINE-CONTRACT.md](PACKET-ENGINE-CONTRACT.md).

## Build and test results

| Check | Result |
| --- | --- |
| CMake configure | **PASS** — Visual Studio 17 2022 x64, Qt 6.12 |
| MSVC Debug build | **PASS** — full build, including Qt AUTOMOC and packet lab targets |
| MSVC Release build | **PASS** — full build, including Qt AUTOMOC and packet lab targets |
| Debug CTest | **PASS** — 3/3: Phase 0–2A regression, packet lab, configuration/report serialization; 10.39 s |
| Release CTest | **PASS** — 3/3: Phase 0–2A regression, packet lab, configuration/report serialization; 10.33 s |
| Qt GUI smoke | **PASS** — latest Release executable deployed with `windeployqt`, launched with restricted PATH, remained responsive for the smoke window, and closed normally with exit code 0 |
| MSVC warnings | No compiler warnings emitted in the full Debug or Release build output. CMake noted optional Vulkan headers are absent; SteamDPI does not use Vulkan. |

The packet tests check IPv4/IPv6 parsing, TCP metadata, TCP/UDP checksums, malformed and truncated input, explicit fragment/extension rejection, source-buffer immutability, explicit-copy independence, no-op behavior, transformation errors, pipeline bounds/lifecycle, deterministic replay results, oversized buffers, and replay count limits. The separate legacy CTest target continues to cover Phase 0–2A regression behavior.

## Offline performance

Release benchmark on the current Windows 11 x64 test machine:

- CPU identifier: `Intel64 Family 6 Model 183 Stepping 1, GenuineIntel`
- Logical processors: 32
- Installed memory: 63.8 GiB
- Workload: 100,000 offline synthetic IPv4 TCP, IPv6 TCP, and IPv4 UDP parse/validation operations through `DryRunTransform`, grouped into timed batches after warm-up.
- Median: **74 ns/packet**; p95: **76 ns/packet** (about 0.074 and 0.076 microseconds).

These values measure local parser/pipeline CPU time for small synthetic fixtures. They are not network latency, handshake latency, throughput under live traffic, or a prediction of future transformations. Timing depends on CPU frequency, compiler and system load.

## Safety invariants

- The packet target does not link to Qt, WinDivert, Winsock, or capture lifecycle code.
- No NETWORK-layer interception, packet send/reinjection, byte mutation strategy, DPI bypass, network setting change, or live traffic test was performed.
- Original `PacketBuffer` data is const. Explicit copy mutation remains isolated from the original.
- Packet size, transform count, replay packet count, and replay byte volume are bounded.
- No real Steam traffic, authentication material, or payload data was used or persisted.
- Ambiguous process/flow identity is outside this lab; the packet engine makes no Steam attribution claim.

## Limitations

- IPv4 options are length/checksum validated but not interpreted. IPv4 fragmented packets, IPv6 extension headers, and IPv6 jumbograms are rejected.
- There are no DPI-desynchronization transforms, packet reinjection, or live capture integration.
- Fixture checksums cover complete synthetic datagrams; there are no external PCAP imports or persistent fixture files.
- GUI smoke was a launch/responsiveness/clean-close check; no packet-lab UI was added.

Phase 2B.1 stops here. Any online packet handling or transformation requires a separate authorization and safety review.
