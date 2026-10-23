# SteamDPI Phase 2B.2 — Controlled Network Integration

**STATUS: PARTIAL** — Stage A read-only NETWORK observation builds and runs, and the Debug/Release suites pass. The live run did not produce a positive NETWORK-packet-to-validated-Steam-flow match, so Steam packet attribution is not accepted. Stage B is blocked by the reinjection safety boundary described below.

## Stage A — Read-only NETWORK capture

Implemented explicit, opt-in WinDivert NETWORK capture with `WINDIVERT_FLAG_SNIFF | WINDIVERT_FLAG_RECV_ONLY`. It observes TCP/UDP and keeps existing FLOW/SOCKET collection independent. Captured bytes are copied into fixed-size bounded events and fed to the offline parser on a separate worker. The receive worker does no synchronous file I/O. Steam attribution requires a unique validated FLOW/SOCKET identity and exact protocol, address, port, direction, and timestamp/lifetime correlation; ambiguous and unknown flows are excluded. No `WinDivertSend` call or packet mutation path is present. Capture remains off until the user starts it.

The NETWORK filter sees general TCP/UDP traffic because WinDivert NETWORK events do not provide a PID. Filtering after capture is necessary to correlate packets with FLOW/SOCKET identity; consequently this mode is not a Steam-only capture filter. Packet bytes exist only in bounded transient memory for parsing and are not logged or persisted.

## Build and automated tests

| Check | Result |
| --- | --- |
| MSVC Debug x64 | **PASS** — full Qt, capture, packet-lab and test build; Qt AUTOMOC ran |
| MSVC Release x64 | **PASS** — full build; Qt AUTOMOC ran |
| Debug CTest | **PASS** — 3/3 tests, 10.35 s |
| Release CTest | **PASS** — 3/3 tests, 10.32 s |
| Qt deployment | **PASS** — `windeployqt` deployed Qt 6 Core/Gui/Network/Svg/Widgets, platform/style/image/network/TLS plugins and compiler runtime |
| GUI smoke | **PASS** — deployed Release GUI launched with PATH restricted to Windows system directories, remained running for the smoke interval, and closed normally (exit 0) |
| Compiler warnings | No compiler warnings in the final Debug/Release incremental build output. `windeployqt` emitted optional DXC and `VCINSTALLDIR` discovery warnings; deployment and launch succeeded. |

CTest includes the Phase 0–2A regression suite, offline packet laboratory, and configuration/report serialization tests. The offline packet lab performance result remains the Phase 2B.1 measurement: median 74 ns/packet and p95 76 ns/packet for its synthetic Release workload. This is local offline processing time, not network latency. See [Phase 2B.1 report](PHASE-2B1-REPORT.md).

## Live observation

An elevated observation helper was launched through normal Windows UAC for a 20-second sample. The WinDivert FLOW/SOCKET and NETWORK handles initialized and the test completed without a driver error. The Steam Store TCP connectivity check succeeded while observation was active.

| Metric | Observed result |
| --- | ---: |
| NETWORK packets received | 22,156 |
| Parsed / rejected | 21,692 / 464 |
| Packets matched to validated Steam flows | **0** |
| Unmatched / ambiguous | 21,692 / 0 |
| Internal monitoring queue drops | 0 |
| Captured bytes / average rate | 23,366,311 / 1,168,315 bytes/s |
| Maximum capture / parse latency | 6.3096 ms / 33.9 µs |
| NETWORK IPv4 / IPv6 packets parsed | Not recorded by the retained output for this sample; live FLOW counters showed both families (IPv4 1, IPv6 1) |
| FLOW/SOCKET attribution | 2 Steam-related flows (both `steamwebhelper.exe`), 55 unknown/ambiguous; observed flow counters: TCP 0, UDP 2, IPv4 1, IPv6 1 |
| Resource samples | Working set 6.77 MB initial, 12.70 MB peak, 13.06 MB final; normalized CPU peak 0.177% on 32 logical processors |
| Handle samples | 69 initial, 239 peak, 224 final; capture worker threads after shutdown: 0 |

The live helper also completed 10/10 Start/Stop cycles and destruction of an active controller. Handles returned close to baseline. No SteamDPI capture worker remained after shutdown. The observed FLOW/SOCKET identities show that process attribution was active, but the separate NETWORK packets did not correlate to them. This gap means the requirement to verify Steam-specific packet attribution has not passed. The sample's zero ambiguous count does not make unmatched traffic Steam traffic. Zero internal queue drops measures only SteamDPI's bounded queue; it says nothing about loss elsewhere in the network stack.

No payloads or packet contents were persisted. No DNS, proxy, firewall, hosts, or other network configuration was changed. No WinDivert service was stopped or deleted. The live run is a bounded observation, not proof that all applications or all Steam connections remain unaffected.

## Stage B — Isolated loopback no-op integration

**BLOCKED — not implemented or run.** Stage A uses SNIFF/RECV_ONLY, which cannot provide a receive/reinject cycle. A reinjection experiment would require a different diverting handle and a `WinDivertSend` path. The WinDivert documentation describes reinjection and loopback behavior with interactions that can include other Windows Filtering Platform callouts; this environment has not established that an exact-endpoint loopback experiment is isolated from those components. The task explicitly requires blocking Stage B when reinjection cannot be isolated reliably, so no non-SNIFF handle, reinjection code, or experiment executable was added. Stage A remains read-only and usable.

WinDivert documents its layers, flags, address metadata, checksum offload behavior, and reinjection considerations in the [official WinDivert documentation](https://github.com/basil00/WinDivert/wiki/WinDivert-Documentation). Packet checksum validation accounts for WinDivert offload metadata; packets lacking a verifiable checksum indication are counted as unverified instead of being treated as confirmed valid.

## Limitations and remaining work

- Stage A has functional live capture, bounded parsing, and safe shutdown evidence, but no successful Steam NETWORK-flow correlation was observed. Investigate tuple canonicalization, direction normalization, and timestamp/lifetime association with additional controlled captures before claiming Steam attribution.
- NETWORK capture observes system TCP/UDP at the NETWORK layer before post-capture classification; it cannot guarantee Steam-only observation. Unknown traffic is excluded from Steam metrics.
- The live run exercised IPv4 and IPv6 packets, but did not establish positive Steam attribution for either family.
- CPU and working-set values are a short, machine-specific sample, not a sustained-load benchmark. Offline packet-lab latency is reported separately.
- Stage B loopback equivalence, timeout/cancellation, and post-exit non-interception checks remain untested because the safe isolation precondition was not met.

No Phase 3 work was started and no Git push was performed.

