# SteamDPI Phase 2B.2.3 — Live Acceptance (2026-10-23)

**STATUS: PARTIAL**

The elevated Release acceptance helper completed a 120-second read-only WinDivert capture using the default `VerifiedSteamEndpointCandidates` profile. It verified two live `steamwebhelper.exe` flows, but matched zero NETWORK packets to a verified Steam flow. The acceptance requirement for at least one actually verified Steam packet is therefore unmet. No packet modification, reinjection, DPI bypass, network configuration change, or Git push was performed.

## Environment and lifecycle

- Administrator token: confirmed (`DATHUADESKTOP\diepy`, `IsInRole(Administrator)=True`).
- Workspace: `C:\Users\diepy\OneDrive\Desktop\SteamDPI`; the `CMakeLists.txt`, SteamDPI sources, pinned WinDivert 2.2.1 files, Release helper, and target report were present. This supplied directory has no `.git` metadata, so Git could not independently identify a checkout or report status.
- Helper: existing `out/build/windows/Release/tests/steamdpi_live_capture_acceptance.exe`; no helper source or application code was changed.
- Store connectivity during live capture: `Test-NetConnection store.steampowered.com -Port 443` succeeded (`TcpTestSucceeded=True`, remote endpoint `23.77.15.30:443`).
- Ten Start/Stop cycles passed. The helper then stopped the live capture cleanly, destroyed an active controller, and reported clean handle shutdown. After shutdown there were 5 process threads and 0 named SteamDPI capture workers.
- WinDivert observation remained read-only (FLOW/SOCKET plus NETWORK SNIFF/RECV_ONLY).

## Live measurements

| Measure | Phase 2B.2.3 live sample |
|---|---:|
| Observation duration | 120 s |
| Verified Steam flows | 2 (`steam.exe`: 0; `steamwebhelper.exe`: 2; UDP: 2; IPv4: 2) |
| Unknown or ambiguous flows | 151 |
| NETWORK packets received | 27,947 |
| Filtered before full parser | 27,928 (18,524 without verified flow scope; 9,404 outside endpoint candidate scope) |
| NETWORK events enqueued / dequeued | 19 / 19 |
| Parsed / rejected | 7 / 12 (all 12 rejected for checksum) |
| Finalized for correlation | 7 |
| Exact endpoint address candidates / matched Steam packets | 0 / 0 |
| Exclusive mismatch | 7 local-address mismatches; all other mismatch categories 0 |
| NETWORK / FLOW-SOCKET / correlation queue drops | 0 / 0 / 0 |
| NETWORK queue high-water | 1 / 128 |
| FLOW-SOCKET queue high-water | 87 / 512 |
| Correlation queue high-water | 2 / 4,096 |
| Max capture / parse latency | 1,099,900 ns / 2,000 ns |
| CPU peak | 0.038993% |
| Working set initial / peak / final | 6.11 / 29.58 / 12.73 MB |
| Handles initial / peak / final | 72 / 246 / 229 |

The 27,928 pre-parser filters equal received packets minus 19 NETWORK events enqueued. No internal queue reported drops during this sample. The helper's resource and lifecycle checks passed.

## Comparison and address analysis

The preceding verified-endpoint profile sample (Phase 2B.2, 60 seconds) saw 16,506 NETWORK packets, 16,231 pre-parser filters, 275 events enqueued/dequeued, 121 parsed and finalized, and 0 queue drops. It observed 12 verified flows and 121 exclusive local-address mismatches. This 120-second sample received 27,947 packets, filtered 27,928 before parsing, and enqueued only 19 events; it again had 0 internal queue drops and 0 matched Steam packets. Because traffic and duration differed, the samples are operational observations, not a controlled performance benchmark. The earlier broad-capture baseline (Phase 2B.2.2) delivered 118,793 NETWORK events to the parser worker and recorded 1,212 NETWORK event queue drops plus 32,469 correlation queue drops over 120 seconds; the optimized profile continues to reduce parser input and avoids those internal queue drops in the observed runs.

For the seven packets finalized this run, protocol/family and port candidate checks did not reject them; the exclusive first failure was the local address comparison. Independent exact local and remote address candidate counts were both zero, as was the word-swapped-address diagnostic. No raw addresses or tuples were saved, so the capture data establishes that neither packet endpoint representation matched the verified flow addresses under the implemented normalization, but it cannot distinguish NAT, proxying, tunnel/interface translation, or another NETWORK-versus-FLOW/SOCKET representation difference. That cause remains unresolved. A port, hostname, remote IP, or looser tuple rule would not establish Steam packet identity and was not used.

## Acceptance decision

**PARTIAL.** Administrator, Release helper, optimized candidate profile, read-only capture, Store connectivity, bounded queues, resource sampling, Start/Stop, handle cleanup, and worker shutdown checks passed. Live verified Steam flows were observed, but no NETWORK packet passed exact endpoint and verified process correlation. Do not claim SUCCESS or advance to Phase 3 on this evidence.

---
# SteamDPI Phase 2B.2.2 — Correlation and Queue Report

**STATUS: PARTIAL**

The verified-endpoint candidate profile, mismatch diagnostics, and queue instrumentation are implemented. Debug/Release builds and CTest pass. The elevated live sample completed with bounded queues and no observed connectivity disruption, but produced zero fully matched Steam NETWORK packets. New Steam flows occurred and address matching remained zero, so acceptance is **PARTIAL**.

## Root cause and evidence

The prior 120-second broad-capture sample reported 118,793 NETWORK events delivered to the parser worker, 110,093 parsed, 1,212 earlier NETWORK event queue drops, 32,469 metadata-correlation queue drops, 3,917 verified-Steam protocol candidates, 51 protocol-and-port candidates, and zero exact-address candidates or matched Steam packets. Thus at least 120,005 NETWORK callbacks reached the controller before queue overflow, while 118,793 reached the parser worker. Independent local and remote address candidate counters were both zero; the word-swapped address diagnostic was also zero. Protocol/family and both packet port orders were checked, so simple direction reversal alone did not explain the mismatch. Canonical IPv4-mapped and IPv6 behavior has deterministic fixtures.

This evidence establishes that the packet endpoint addresses differ from the addresses stored in the verified Steam flows for the port-pair candidates observed. It does not establish why. IPv4 byte order, mapped-address conversion, IPv6 comparison, and local/remote reversal are covered by deterministic tests and did not explain the prior live sample. A different path at the NETWORK layer, such as NAT, a proxy, or a tunnel interface, remains possible. Stale flows and event timing were also inspected; lifecycle fixes already accept bounded event-order skew, retain timestamped close records, and drain capture before stop. Different connections sharing ports remain possible, which is why ports are only a candidate filter and never sufficient for attribution. No raw live addresses were persisted.

The WinDivert adapter opens read-only FLOW, SOCKET, and NETWORK SNIFF/RECV_ONLY handles with the broad `tcp or udp` kernel filter. A verified PID cannot be expressed in a NETWORK-layer filter, and a static endpoint filter could miss a new Steam connection. The new profile therefore performs a bounded fixed-header check after receive but before full parsing and the NETWORK queue. Once validated Steam FLOW/SOCKET identities exist, only same-family, same-protocol packets with the observed local/remote port pair enter full parsing. Direction remains available for mismatch classification. Exact Steam attribution still requires the complete normalized local/remote addresses and ports, direction, time window, and validated executable identity.

## Implementation changes

- `NetworkCaptureProfile` provides `AllTcpUdp` and `VerifiedSteamEndpointCandidates`; the application defaults to the verified Steam candidate profile, while tests can select the broad profile.
- The FLOW/SOCKET worker publishes immutable snapshots of verified protocol/family/port scopes. The NETWORK receive callback reads an atomic shared snapshot rather than locking and scanning the flow table for every packet.
- Candidate filtering rejects unrelated packets before full packet parsing. Metrics distinguish packets filtered because no verified flow was known from packets outside verified endpoint-port scope.
- Captured packet accounting distinguishes pre-parser filters, NETWORK queue overflow, parser valid/rejected outcomes, and packets discarded after capture became inactive.
- Endpoint mismatch results now distinguish local/remote port and local/remote address mismatch. A finalized packet receives one exclusive rejection category; combined port/address counters equal their detailed subcategories plus the general category. Candidate totals are diagnostic counts and may count more than one candidate per packet; they are labeled separately.
- Bounded queues report successful enqueue/dequeue counts and high-water occupancy. The documented capacities remain 512 FLOW/SOCKET events and 128 NETWORK events; overflow drops the newest event and increments the matching drop counter. The metadata-only correlation queue remains bounded at 4,096 and reserves its storage at controller construction. Packet bytes are moved into the parser buffer; pending correlation retains metadata only.
- Metrics include received packets, pre-parser filtered packets, parsed/rejected packets, each queue's drops and high-water mark, correlation queue high-water and completion count, capture/parse/correlation latency, and producer/consumer rates in the live helper. The live helper continues to sample working set, handles, and CPU.
- Debug builds print only synthetic canonical examples: IPv4 `::ffff:c000:0201 -> ::ffff:c633:6402`; IPv6 `2001:db8::1 -> 2001:db8::2`.

## Before/after comparisons

| Measure | Previous broad live sample | Current implementation |
|---|---:|---|
| NETWORK callbacks received | At least 120,005 | 16,506 |
| Pre-parser filtered | Not measured | 16,231 (98.3%) |
| Delivered to parser / parsed | 118,793 / 110,093 | 275 / 121 |
| Parser rejected | 8,700 | 154 (all checksum) |
| Capture queue drops / correlation drops | 1,212 / 32,469 | 0 / 0 |
| Verified Steam flows (`steam.exe` / `steamwebhelper.exe`) | 2 (0 / 2) | 12 (10 / 2) |
| Verified-Steam protocol/port candidates | 3,917 / 51 | 280 / 174 |
| Exact address candidates / fully matched packets | 0 / 0 | 0 / 0 |

The prior values are baseline evidence only and must not be read as results from the new profile. Queue saturation tests verify a fixed upper bound, newest-item drop behavior, recovery on stop, and balanced packet accounting; they do not predict live throughput.

## Regression verification

- MSVC 2022 x64 Debug build: **PASS**.
- MSVC 2022 x64 Release build: **PASS**.
- Debug CTest: **3/3 passed** (`steamdpi_tests`, `steamdpi_packet_lab_tests`, `steamdpi_config_tests`).
- Release CTest: **3/3 passed** (same targets).
- Qt AUTOMOC ran for GUI and test targets in both configurations.
- Deterministic coverage includes IPv4 byte order and mapped IPv4, IPv6 canonical matching, TCP/UDP both directions, reversed tuples, local/remote mismatch classification, unrelated connection sharing the remote port, unknown identity rejection, PID reuse, stale/closed flow windows, strict pre-parser profile, queue saturation/accounting, repeated lifecycle, and overload shutdown.
- The Release GUI was deployed with `windeployqt`, launched with PATH restricted to Windows system directories, created a window handle, responded, and closed via `WM_CLOSE` with exit code 0.
- `Test-NetConnection store.steampowered.com -Port 443` returned `True`.
- Elevated live Start/Stop, resource sampling, and attribution verification: **PASS** for lifecycle/resource checks; **PARTIAL** for attribution because no packet matched. The UAC prompt was approved for this run.
- Build output showed no compiler warnings in the completed Debug/Release builds. `windeployqt` reported its optional Direct3D compiler and `VCINSTALLDIR` warnings; deployment and isolated GUI launch succeeded.

## Live acceptance and remaining risks

The existing Release helper ran for its bounded 60-second window under standard Windows elevation. It completed its ten Start/Stop cycles, active-controller destruction, and final handle shutdown. During capture, `Test-NetConnection store.steampowered.com -Port 443` returned `True`.

The helper recorded 16,506 NETWORK callbacks; 16,231 (98.3%) were filtered before full parsing: 13,121 while no verified Steam flow scope existed and 3,110 outside known protocol/family/port scopes. The remaining 275 events entered the NETWORK queue (high-water 1/128, no drops); 121 parsed and 154 were rejected, all for checksums. The parsed count plus parser rejects equals the NETWORK events dequeued. The correlation queue reached 21/4,096 entries, finalized all 121 queued candidates, and dropped none. FLOW/SOCKET queues reached 91/512 and also had no drops. Received traffic averaged about 275 packets/s, pre-parser filtering about 270.5/s, and parser input about 4.6/s. This demonstrates the intended reduction for this host/window; it is not a general throughput guarantee.

The sample observed 12 verified Steam flows: 10 `steam.exe` and 2 `steamwebhelper.exe` (5 TCP, 7 UDP; all 12 IPv4). It also observed 126 unknown/ambiguous flows. There were 280 Steam protocol candidates and 174 protocol-and-port candidate observations. Of 121 packets finalized for correlation, all 121 were classified at the first-failure stage as local-address mismatches; the exclusive remote-address, port, protocol, direction, time-window, identity, and family rejection categories were zero. Independent local and remote exact-address candidate counters were both zero, as was the word-swapped diagnostic. Exact address candidates and fully matched Steam packets were both zero. Thus verified new Steam flow events occurred, but the NETWORK endpoint representation/path still did not match their addresses. NAT, proxy, tunnel/interface behavior, or another endpoint representation difference remains possible; this sample does not prove which.

Resource sampling during the run measured CPU peak at 0.0195%; working set was 6.10 MB initially, 13.65 MB peak, and 12.48 MB after shutdown. Handles were 68 initially, 239 peak, and 227 after shutdown. The helper reported five process threads after shutdown and zero named SteamDPI capture workers. Ten Start/Stop cycles passed. Capture latency maximum was 288,200 ns; parser latency averaged 2,498 ns with a 3,800 ns maximum; correlation latency averaged 510,170,235 ns. No connection interruption was observed. The prior broad sample's 0.0585% CPU peak, 6.77/13.65/12.69 MB working set (initial/peak/final), and 69/243/229 handles are baseline measurements, not directly comparable because system traffic and sample lengths differed.

The older broad sample had at least 120,005 callbacks including 1,212 event-queue drops, 118,793 events delivered to the parser, 32,469 correlation drops, and 51 verified-Steam protocol-and-port candidate observations with zero address candidates or matches. The new profile reduced parser input from 118,793 in 120 seconds to 275 in 60 seconds and eliminated internal queue drops in this sample. Host traffic differed, so this before/after comparison shows observed operation rather than a controlled benchmark. Attribution itself remains unresolved and unaccepted.

The broad kernel filter still receives TCP/UDP packets before user-mode pre-parser filtering. This is required so dynamic Steam identities can be learned without unsafe static IP/port assumptions. WinDivert receive callbacks perform only bounded header peeking, an atomic scope read, and a bounded queue push; full parsing and correlation stay on workers. Internal queue counters describe SteamDPI's own queues and do not measure system packet loss.

The correlation repair is not accepted as live-working until an elevated sample on this build produces at least one NETWORK packet matched by exact tuple, time window, and verified process identity. Do not weaken the classifier or infer Steam from a port, hostname, or remote IP to obtain a match. No packet modification, blocking, reinjection, persistent network changes, or Git push was performed.
