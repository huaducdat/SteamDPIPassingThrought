# Phase 0–1 Final Acceptance Report

**STATUS: SUCCESS** — Windows x64 Debug and Release builds, CTest, Qt GUI launch, cancellation/shutdown checks, and isolated Qt DLL deployment all completed successfully. DPI bypass remains unimplemented.

## Toolchain and build

| Check | Result |
| --- | --- |
| Visual Studio | Community 2022 17.14.7, registered at `C:\Program Files\Microsoft Visual Studio\2022\Community` |
| MSVC | v143 x64/x86, toolset 14.44.35207; compiler 19.44.35211 |
| Windows SDK | Windows 11 SDK 10.0.26100.0 |
| CMake / Ninja | CMake 4.4.4 / Ninja 1.11.0 |
| Qt | Qt 6.12.0 MSVC 2022 x64 at `C:\Qt\6.12.0\msvc2022_64` |
| CMake configure | `cmake --preset windows` — passed with Visual Studio 17 2022 x64 |
| Debug build | `cmake --build --preset debug --parallel 8` — passed |
| Release build | `cmake --build --preset release --parallel 8` — passed |
| AUTOMOC | Ran for SteamDPI, core tests, live diagnostic tool, and configuration tests; application linked successfully |
| Compiler warnings | No MSVC compiler warnings in final Debug or Release build output |

The Windows preset supplies `CMAKE_PREFIX_PATH=C:/Qt/6.12.0/msvc2022_64`. CMake reported that optional Vulkan headers (`WrapVulkanHeaders`) were unavailable; SteamDPI does not use Vulkan. `windeployqt` also warned that `dxcompiler.dll`/`dxil.dll` were absent, which is only relevant to Direct3D 12 features not used here.

## Automated tests

| Configuration | Result |
| --- | --- |
| Debug CTest | **2/2 passed**, 10.25 seconds |
| Release CTest | **2/2 passed**, 10.26 seconds |

The fixture suite covers DNS lookup/failure, loopback TCP connect/refused/timeout, orchestration cancellation, TLS stall timeout, and cancellation during an active TLS handshake. The Qt configuration/report test rejects malformed and out-of-range JSON, accepts valid configuration, and verifies serialized report fields including `engineState=NOT_IMPLEMENTED`. No public Internet access is needed by these CTest cases.

WinHTTP on this host rounds very short per-phase timeouts up. The 500 ms TLS stall fixture therefore completes within the recorded 5-second bound rather than exactly at 500 ms. The production default timeout is longer. Active TLS cancellation is classified as `Cancelled`; the GUI stays responsive while its worker completes cancellation and cleanup.

## GUI, runtime, and deployment

- Launched the Release GUI from the deployed directory with its process `PATH` restricted to Windows system directories. The window title was `SteamDPI — Network Diagnostics`; Qt Core, Gui, Widgets, and `qwindows.dll` loaded from `out/deploy/Release`.
- `windeployqt --release --compiler-runtime` deployed Qt 6.12 Core/Gui/Network/Svg/Widgets, the Windows platform plugin, required image/style/network/TLS plugins, Qt translations, and the MSVC runtime installer. The deployed executable closed normally with exit code 0.
- UI Automation verified Run/Cancel state changes. A cancel during an active diagnostic run returned the UI to the idle state in 611 ms. Repeated Run/Cancel cycles had also been exercised without a crash. Closing the launched GUI while diagnostics were starting returned exit code 0 in 59 ms.
- The tests join their local fixture worker threads, and the GUI exits without a surviving process. No dedicated long-duration heap/handle leak instrumentation was run; resource cleanup is verified functionally through bounded cancellation, fixture teardown, and normal process shutdown.
- Steam is not required to be installed. No Administrator rights are needed for ordinary diagnostics.

## Steam Store observation

Bounded live observations performed on 2026-10-22 did not establish a cause for the Store connection issue. App-path probes resolved `store.steampowered.com` and connected TCP/443, then timed out during WinHTTP TLS after approximately 4.0 seconds and 8.0 seconds on separate runs. No HTTP response or certificate-validation result was reached.

A separate address-pinned curl/Schannel observation resolved A record `23.77.15.30` and no AAAA records. Two attempts at each 4-second and 8-second timeout expired before TCP connection completed; TLS and HTTP were not reached. Since those observations used a separate connection path and had a different TCP outcome, they do not prove intermittent behavior or DPI interference. The application reports stage-specific outcomes and does not infer DPI from a timeout.

## Scope and remaining limitations

- DPI Start/Stop remain disabled and engine state remains `NOT_IMPLEMENTED`.
- No WinDivert handle/driver, packet capture/manipulation, network setting, firewall, proxy, DNS, hosts file, Windows service, or certificate store was changed.
- Local deterministic tests cover TLS cancellation and timeout. A local trusted/untrusted certificate server fixture is not included; certificate/hostname validation remains enabled by WinHTTP and a successful live external TLS handshake was not observed in the Steam Store probe.
- No dedicated leak detector or multi-hour soak test was run.

Phase 0–1 acceptance is complete for the implemented scope. Do not begin Phase 2 until separately authorized and reviewed.
