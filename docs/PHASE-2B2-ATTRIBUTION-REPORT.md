# SteamDPI Phase 2B.2.1 — Network Attribution Repair Report

**STATUS: PARTIAL**

The deterministic correlation defects found in the capture pipeline are fixed and covered by tests. Live read-only observation still produced zero Steam NETWORK packet matches. Therefore the acceptance requirement for a real validated Steam flow match has not passed, and attribution must remain strict and unresolved.

## Root-cause investigation

Three correlation lifecycle defects were confirmed in the implementation. NETWORK and FLOW/SOCKET records are consumed by independent workers, while packets were initially classified immediately; a packet processed before its identity event could never be reconsidered. FLOW_ESTABLISHED can be timestamped after the NETWORK packet that caused it, but the old comparison rejected that small timestamp inversion. Finally, FLOW Deleted and SOCKET Close removed records immediately, so queued packets could lose their flow before classification. The controller also entered Stopped before draining its NETWORK queue, discarding captured packets during stop.

The controller now retains parsed metadata in a bounded, 500 ms correlation window, accepts a bounded 500 ms timestamp inversion for flow creation, retains timestamped close records for matching packets within the flow lifetime, and drains captured NETWORK events before changing to Stopped. The pending metadata queue limit is 4,096 entries. It contains no packet bytes or payload.

These defects explain missed packets around event ordering and shutdown, but do not explain the persistent live address mismatch. The latest 120-second sample found 3,917 verified-Steam protocol candidates and 51 protocol-and-port candidates, but **zero** endpoint-address candidates. Both the independent Steam local-address and remote-address candidate counters were zero. Exact address tuple matching therefore remains the unresolved live root cause. The sample also had 32,469 correlation queue drops during capture bursts, so some packets were not classified. No unmatched packet was promoted to Steam.

WinDivert documents FLOW/SOCKET IPv4 endpoints as IPv4-mapped IPv6 addresses and timestamps as sharing the QueryPerformanceCounter clock. Its helper API describes address-word conversion between host-byte and network-byte order. The implementation retains canonical network-byte normalization and tests IPv4-mapped conversion. The live results indicate that this normalization alone does not yet yield endpoint equality. [Official WinDivert documentation](https://github.com/basil00/WinDivert/wiki/WinDivert-Documentation), [WinDivert helper byte-order documentation](https://www.reqrypt.org/windivert-doc.html).

## Changes

- `src/capture/CaptureController.cpp`: bounded correlation backlog increased to 4,096 metadata entries; prior race, timestamp-skew, and stop-drain handling retained.
- `src/capture/FlowTracker.cpp` and `src/capture/FlowTracker.h`: aggregate local/remote address candidate diagnostics for verified Steam flows, while exact tuple matching remains unchanged.
- `src/capture/CaptureTypes.h`: corresponding aggregate metrics.
- `tests/live_capture_acceptance.cpp`: prints the additional aggregate endpoint candidate counters.
- `tests/test_main.cpp`: verifies deterministic local, remote, and exact Steam address candidate counts.

## Deterministic verification

- MSVC 2022 x64 Debug build: **PASS**.
- MSVC 2022 x64 Release build: **PASS**.
- Qt AUTOMOC: invoked for the GUI and test targets in both configurations; builds passed.
- Debug CTest: **3/3 passed** (`steamdpi_tests`, `steamdpi_packet_lab_tests`, `steamdpi_config_tests`).
- Release CTest: **3/3 passed** (same three tests).
- Fixtures cover TCP/UDP inbound and outbound correlation, IPv4/IPv6, mapped IPv4 address normalization, event order and timestamp skew, PID reuse, simultaneous ambiguity, expiration, close tombstones, and pending correlation timeout.
- The GUI executable launched and remained responsive for a three-second smoke interval. It was then terminated by the smoke-test command; this was a launch check, not a clean user-close test.

These tests establish deterministic behavior only. They do not prove live Steam attribution.

## Live observation

Two bounded elevated read-only captures were run after the backlog change. The Steam client was already running and was not restarted. The adapter used NETWORK/FLOW/SOCKET SNIFF and RECV_ONLY; no WinDivertSend call, packet transformation, payload persistence, or network configuration change was made.

| Measurement | 60-second sample | 120-second sample |
|---|---:|---:|
| Steam flows / steam.exe / steamwebhelper.exe | 1 / 0 / 1 | 2 / 0 / 2 |
| Unknown or ambiguous flows | 85 | 178 |
| FLOW / SOCKET events | 151 / 342 | 322 / 768 |
| NETWORK packets captured / parsed / rejected | 24,992 / 19,672 / 5,320 | 118,793 / 110,093 / 8,700 |
| Parsed IPv4 / IPv6 packets | 3,974 / 15,698 | 6,471 / 103,622 |
| Rejections by checksum / truncation / unsupported / length / other | 5,320 / 0 / 0 / 0 / 0 | 8,700 / 0 / 0 / 0 / 0 |
| Steam protocol candidates / protocol+port candidates | 273 / 0 | 3,917 / 51 |
| Steam exact address candidates / matched Steam packets | 0 / 0 | 0 / 0 |
| Unmatched: no flow / protocol / port / address / direction / early / expired / unverified / family | 285 / 1,879 / 14,858 / 747 / 0 / 0 / 0 / 0 / 1,122 | 0 / 1,430 / 37,874 / 38,301 / 0 / 0 / 0 / 0 / 19 |
| Correlation queue drops / NETWORK capture queue drops / FLOW-SOCKET queue drops | 781 / 0 / 0 | 32,469 / 1,212 / 0 |

The 120-second sample recorded 51 verified Steam protocol-and-port candidate observations but no matching local or remote addresses. Its flow totals were 0 TCP / 2 UDP and 1 IPv4 / 1 IPv6. CPU peak was 0.058%; working set was 6.8 MB initially, 13.0 MB peak, and 12.1 MB after capture. Handles were 69 initially, 243 peak, and 229 after shutdown. Ten Start/Stop cycles and destruction while active passed; no SteamDPI capture worker remained after shutdown. The Store HTTPS connectivity check returned `True` during the observation session. No interruption was observed, though this is a bounded sanity check rather than proof of general network safety.

An earlier 120-second sample before the current queue and endpoint-side diagnostics recorded 23 verified `steamwebhelper.exe` flows, 9,086 Steam protocol candidates, 202 Steam protocol-and-port candidates, zero exact addresses, and zero matched packets. Its broader NETWORK capture queue reported 509 drops and its correlation metadata queue reported 61,561 drops. This independently showed that address mismatch persisted when Steam port candidates were present.

## Remaining limitations

- Live attribution remains **unverified**: no newly observed Steam flow was correlated to a NETWORK packet using its validated executable identity.
- The exact reason Steam FLOW/SOCKET addresses do not equal packet endpoints is unresolved. Further investigation needs aggregate, privacy-preserving inspection of the actual normalized tuple representation and direction across a matched flow; raw packet payloads or persisted connection contents are not needed.
- Correlation queue drops remain high under broad host traffic. The 4,096-entry limit is bounded, but the latest 120-second capture still overflowed it. Internal queue-drop counts are not a claim about system packet loss.
- The GUI check verified launch and responsiveness only; it did not validate interactive diagnostics or clean user-driven window close in this run.
- Checksums were the only parser rejection reason in both captures. WinDivert’s checksum validity flags and NIC offload behavior require continued scrutiny; these rejected packets were excluded from attribution.

No Phase 3 work was started. No DPI bypass, reinjection, blocking capture, packet modification, Steam restart, or network setting change was performed.
