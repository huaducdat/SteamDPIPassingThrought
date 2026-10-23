# SteamDPI Phase 2A — Safe Packet Observation

**STATUS: PARTIAL** — Phase 2A.2 attribution, deterministic tests, Debug/Release builds, Qt GUI smoke, elevated live observation, handle-close checks, and connectivity sanity checks passed. The launch-after-capture test was **SKIPPED** because Steam was already running and was left untouched.

## Phase 2A.2 final hardening (2026-10-23)

### Acceptance results

| Check | Result | Evidence |
| --- | --- | --- |
| MSVC Debug build | **PASS** | Full `debug` preset build completed, including Qt AUTOMOC and both live helpers. |
| MSVC Release build | **PASS** | Full `release` preset build completed, including Qt AUTOMOC and both live helpers. |
| CTest Debug | **PASS** | 2/2 tests passed; 10.36 s (final run after last test change). |
| CTest Release | **PASS** | 2/2 tests passed; 10.34 s (final run after last test change). |
| Qt GUI smoke | **PASS** | Deployed Release GUI opened with restricted PATH, showed all three attribution counters, loaded Qt6Widgets/Gui/Core from deployment, and exited with code 0. |
| Elevated WinDivert FLOW/SOCKET observation | **PASS** | Standard UAC elevation; 10 Start/Stop cycles, followed by 30 s live observation. |
| Steam connectivity sanity | **PASS** | Store TCP succeeded before and during capture; Steam kept established connections. No interruption or network-setting change observed. |
| Steam launch after capture starts | **SKIPPED** | Capture became active, then found Steam already running. The helper left Steam untouched and stopped observation normally. |

### Live executable attribution

Counts below are separate real live runs. Unknown/other/ambiguous flows never contribute to the verified Steam-flow total.

| Observation window | `steam.exe` | `steamwebhelper.exe` | Unknown/other/ambiguous | Steam total | TCP | UDP | IPv4 | IPv6 | Errors / dropped monitor events |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 30 s | 2 | 0 | 39 | 2 | 0 | 2 | 2 | 0 | 0 / 0 |
| 15 s repeat | 0 | 3 | 49 | 3 | 3 | 0 | 3 | 0 | 0 / 0 |

Both known executables therefore produced independently attributed live flows across the two runs. Unknown and ambiguous events were counted separately and excluded from Steam totals. The identity resolver still requires a Steam-root path and trusted Valve signature; only `steam.exe` and `steamwebhelper.exe` are eligible for the verified Steam counters. Other images, including the previously recognized `GameOverlayUI.exe`, are no longer included in Steam totals and fall into the untracked/unknown bucket.

### Launch-after-capture test

**SKIPPED (not passed):** `steamdpi_live_launch_after_capture` opened capture and verified `CAPTURING`, then discovered the already-running Steam client/WebHelper. It recorded `SKIPPED: Steam was already running; it was left running unchanged`, stopped capture normally, and did not relaunch or terminate Steam. If Steam is absent, the elevated helper also skips rather than launching Steam with an Administrator token; a future launch-after-capture run requires a separate normal-token launcher.

### Handle, worker, and resource checks

- `WinDivertClose` return values are now checked. A close failure propagates through `CaptureController` as an Error state and Win32 error code rather than being reported as a clean Stop.
- The live Release run passed **10/10 Start/Stop cycles**, active-controller destruction, final Stop, and subsequent handle re-open cycles. The helper reported **0 named SteamDPI capture workers after controller destruction**. The process had 7 total threads at that point, which includes non-capture runtime threads; they are not treated as capture workers.
- A 30-second resource sample took 7 samples: working set **6.78 MB initial / 12.54 MB peak / 12.39 MB final**; process handles **69 initial / 233 peak / 227 final**. Per-cycle handle counts reached 225 after the first cycle and stayed at 225–226 through the tenth cycle; there was no continuing per-cycle growth. Some Windows/runtime handles remained after first-use initialization.
- After all helpers exited, no SteamDPI process or named capture worker remained. SCM still showed the WinDivert kernel service as `RUNNING`; this is a loaded driver service, not evidence of an active SteamDPI observation handle. No service stop/delete was attempted.

### Deterministic test additions

CTest now checks independent Steam client/WebHelper counters, unknown PID and absent identity handling, signer/process-instance mismatches, malformed tuple correlation, and late FLOW/SOCKET events around endpoint shutdown. It verifies these cases increment only the unknown/other/ambiguous counter and never the verified Steam total. A fake adapter close-error fixture confirms that failed handle closure is surfaced and a later clean Start/Stop can recover. Existing queue bounds, cancellation, repeated lifecycle, and fixture coverage remain passing.

The bounded live helpers are `tests/live_capture_acceptance.cpp` and `tests/live_launch_after_capture.cpp`. Their logs contain aggregate counts and resource samples only; no payloads, addresses, ports, or PIDs are persisted.

## Phase 2A.1 live acceptance (2026-10-22; superseded by Phase 2A.2 results above)

- **Administrator runtime:** SteamDPI Release was launched through standard Windows UAC. The elevated token was verified. WinDivert 2.2.1 x64 initialized successfully on FLOW and SOCKET; the kernel driver reported `RUNNING` while capture was active.
- **Live Steam flows:** The bounded, metadata-only live helper observed and attributed **10 flows** through the existing resolver/classifier: **5 TCP, 5 UDP, 5 IPv4, 5 IPv6**. It recorded **0 observation errors** and **0 dropped monitor events**. No addresses, ports, payloads, or process IDs were persisted by the acceptance helper. Aggregate counts do not identify which Steam executable contributed each flow.
- **Steam identities:** The running Steam client and Steam WebHelper images had previously passed the path and trusted Valve signature identity probe. Live aggregate metrics only include events that pass Steam process identity and FLOW/SOCKET correlation. The live run did not record separate per-image counts.
- **Start/Stop and cleanup:** A live controller run completed **10/10 Start/Stop cycles**, then observed for 30 seconds, stopped cleanly, and destroyed a second controller while capture was active. The Qt GUI was separately launched elevated and driven through UI Automation for Start/Stop. Closing the GUI while capture was active exited normally; restarting it created a window and the restarted process exited normally.
- **Connectivity:** Steam already had established TCP connections before capture; it remained running with established TCP connections during observation and after GUI shutdown/restart. TCP to `store.steampowered.com:443` succeeded while observation was on, and a PowerShell-originated TCP check also succeeded with capture active, providing a basic unrelated-application check. No interruption or system network configuration change was observed.
- **Unknown traffic:** No false attribution was observed in the Steam-only aggregate. The resolver requires verified Valve-signed executable identity under a registered Steam root, and deterministic tests cover unrelated/unknown identities. The live UI exposes Steam aggregates rather than an all-process event audit.
- **Failure handling:** A non-elevated UI start had previously been rejected with an explicit Administrator-required status before loading the WinDivert DLL. Fake-adapter tests cover driver open/receive failures and recovery. A live driver-failure injection was not attempted because the driver initialized successfully and there was no need to disturb the machine.
- **Not exercised in the Phase 2A.1 run:** Steam was already running and the older helper did not record per-binary flow counts. Phase 2A.2 separately added exact per-binary counts and attempted launch-after-capture; that test remains **SKIPPED** because Steam was already running, as detailed above.
- **Driver cleanup note:** No SteamDPI or live-helper process remained after shutdown. The Service Control Manager still reported the WinDivert kernel service as `RUNNING`; no manual driver/service unload was attempted.

The live helper is `tests/live_capture_acceptance.cpp`. Its output contains bounded aggregate metrics only; the recorded run is under the ignored build output directory.

## WinDivert version and provenance

- Pinned upstream release: **WinDivert 2.2.1, x64**, asset `WinDivert-2.2.1-A.zip` from the [official release](https://github.com/basil00/WinDivert/releases/tag/v2.2.1).
- Upstream also lists 2.2.2, but an [upstream issue reports a Microsoft Defender quarantine for its 2.2.2-A archive](https://github.com/basil00/WinDivert/issues/411). That report does not establish the artifact is malicious or that 2.2.1 is risk-free; 2.2.1 was selected as the inspected pin and no matching local Defender detection was returned.
- The downloaded archive SHA-256 is recorded in [`third_party/windivert/2.2.1/PROVENANCE.md`](../third_party/windivert/2.2.1/PROVENANCE.md). Release metadata does not publish an independent archive digest.
- Included only the x64 header, DLL, SYS driver, version, and license. No sample executables, x86 files, or WinDivert command-line tools are bundled.
- PE inspection reports machine `8664 (x64)` for both DLL and SYS. The driver Authenticode verification returned **Valid** with thumbprint `043589F75FCE2795E7F2CC3E526D46784D5DDAB3`; its certificate subject is `CN=成都密思听科技有限公司`. `WinDivert.dll` has no embedded signature; its exact SHA-256 is pinned in the provenance file. Defender real-time protection was enabled; a read-only query returned no matching WinDivert/SteamDPI detection.
- The official [WinDivert documentation](https://github.com/basil00/WinDivert/wiki/WinDivert-Documentation) describes FLOW as process-attributed flow lifecycle events, SOCKET as process-attributed socket operations, and NETWORK as lacking PID information. It also documents the required FLOW flags and receive-only behavior.

## Implementation

- `WinDivertAdapter` loads the local pinned DLL only after an explicit capture request and opens FLOW and SOCKET handles with `WINDIVERT_FLAG_SNIFF | WINDIVERT_FLAG_RECV_ONLY`. It does not open a NETWORK handle. It calls `WinDivertRecv` with a null packet buffer to receive event metadata only.
- No `WinDivertSend`, packet transformation, dropping, reinjection, payload parsing, or payload persistence is implemented. FLOW/SOCKET filters select TCP/UDP events; non-Steam metadata is immediately excluded from Steam metrics unless process and flow correlation succeeds.
- `SteamTrafficClassifier` counts a flow only when FLOW and SOCKET events match endpoint and parent endpoint IDs, process ID and creation time, verified image identity, and full protocol/address/port tuple. It independently classifies `steam.exe` and `steamwebhelper.exe`; unknown, unsupported, and ambiguous flows increment their separate counter and never the verified Steam total.
- `ProcessIdentityResolver` checks Steam registry install roots, resolves the canonical process image path, requires a Valve organization in a trusted Authenticode signature, and accepts only `steam.exe` and `steamwebhelper.exe` for the verified attribution counters. It does not use process names alone. The actual running `steam.exe` and `steamwebhelper.exe` were both accepted from `C:\Program Files (x86)\Steam` by the read-only identity probe.
- Capture uses a bounded event queue and bounded flow table. UI counters are observational flow counts. “Dropped monitoring events” refers to application-side monitoring queue/table overflow; it is not a WinDivert kernel queue or network packet-loss statistic.
- Start reports a clear Administrator requirement, driver missing/blocked/signature/incompatible errors, or DLL/API mismatch. The program does not silently elevate. Stop shuts down both receive handles and joins the reader threads; application shutdown calls Stop.

## Build and automated tests

| Check | Result |
| --- | --- |
| CMake configure | **PASS** — Visual Studio 17 2022 x64, Qt 6.12 MSVC x64 |
| MSVC Debug full build | **PASS** — `cmake --build --preset debug --parallel 8` |
| MSVC Release full build | **PASS** — `cmake --build --preset release --parallel 8` |
| Qt MOC/AUTOMOC | **PASS** — GUI and Qt configuration test targets built |
| Debug CTest | **PASS** — 2/2 tests, 10.36 seconds (final run) |
| Release CTest | **PASS** — 2/2 tests, 10.34 seconds (final run) |
| Compiler diagnostics | No MSVC compiler warnings were emitted in the final build output. CMake noted optional Vulkan headers are not installed; SteamDPI does not use Vulkan. |

The fixture tests cover Steam executable path/signature policy, FLOW/SOCKET tuple and PID correlation, PID-reuse rejection, ambiguous/unattributed flow exclusion, IPv4/IPv6 and TCP/UDP counters, bounded queues, repeated Start/Stop, open and receive failure recovery, and controller destruction while capture is active. Existing diagnostics and configuration/report serialization tests also passed. These tests use fake adapters and prove controller/classifier behavior only; they do not claim driver operation.

## GUI and runtime evidence

- The Release GUI was deployed with `windeployqt` and the pinned `WinDivert.dll`, `WinDivert64.sys`, and license in the same directory. It launched with `PATH` restricted to Windows system directories; Qt DLLs and `qwindows.dll` loaded from the deployment directory. The process closed normally with exit code 0.
- UI Automation found the Packet Observation controls. Clicking Start Capture without elevation produced an explicit Administrator-permission message; the WinDivert DLL was not loaded and no driver handle was opened. Existing Run Diagnostics and Cancel controls continued to respond.
- The current interactive token was not elevated. The live observation and 10-cycle lifecycle checks were performed in a separate helper through standard Windows UAC elevation; the driver service remained loaded afterward, while no SteamDPI capture handle, helper process, or named SteamDPI capture worker remained active.
- Phase 2A.2 observed real FLOW/SOCKET events and separately checked Steam Store TCP before and during capture. Exact per-binary counts and the skipped launch-after-capture test are recorded above.

## Safety, limitations, and remaining risks

- No payload is received or retained. No NETWORK-layer packet capture, packet mutation, drop, reinjection, redirection, firewall/proxy/DNS/hosts change, or Git push occurred. The DPI bypass controls remain disabled.
- Unknown and uncorrelated traffic never contributes to Steam counters. Actual executable path and signer validation passed for the running Steam client and web helper; Phase 2A.2 live counters independently observed both identities.
- The third-party driver is kernel-mode software. Its signature and provenance were checked, and Phase 2A.2 verified initialization and handle lifecycle on this Windows 11 installation. An explicit Start Capture request from an already elevated process is required before the WinDivert API can open its handles.
- WinDivert does not expose network packet-loss counts through these event layers. The UI does not claim packet loss or protection/bypass status.
- The pinned release and C++ adapter use only FLOW/SOCKET event layers. Phase 2B, DPI bypass, and any packet-processing engine are not implemented.

The remaining acceptance gap is the explicitly **SKIPPED** launch-after-capture scenario: Steam was already running, and the test intentionally did not stop/restart it or launch it with an elevated token. This is not counted as a pass. Other live checks and their exact results are recorded in the Phase 2A.2 section above.
