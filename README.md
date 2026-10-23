# SteamDPI

SteamDPI is a Windows 11 x64 desktop application for diagnosing DNS, TCP/443, and validated HTTPS/TLS connectivity to a fixed set of Steam web hostnames. The DPI engine remains `NOT_IMPLEMENTED`.

## Requirements

- Windows 11 x64
- Visual Studio 2022 with the MSVC x64 C++ tools and Windows SDK
- CMake 3.25 or newer
- Qt 6.12 Widgets built for MSVC 2022 x64
- Ninja is optional when using the Visual Studio CMake presets

The Windows preset expects Qt 6.12.0 MSVC 2022 64-bit under `C:\Qt` and supplies `CMAKE_PREFIX_PATH` for that kit. In Qt Maintenance Tool, select the MSVC 2022 64-bit package; MinGW is a separate, incompatible toolchain.

## Build, test, and run

From a Developer PowerShell for VS 2022 with Qt 6.12's MSVC kit available to CMake:

```powershell
cmake --preset windows
cmake --build --preset debug
ctest --preset debug
cmake --build --preset release
ctest --preset release
& .\out\build\windows\Release\SteamDPI.exe
```

To create a standalone Release folder with Qt runtime DLLs and plugins, run:

```powershell
& 'C:\Qt\6.12.0\msvc2022_64\bin\windeployqt.exe' --release --compiler-runtime --dir .\out\deploy\Release .\out\build\windows\Release\SteamDPI.exe
& .\out\deploy\Release\SteamDPI.exe
```

The `windows-core-tests` preset builds local network fixtures without Qt. The full test preset also builds JSON configuration and report serialization checks.

To run live checks against the fixed registry after building, run:

```powershell
& .\out\build\windows\Release\tests\steamdpi_live_diagnostics.exe
```

This makes real DNS, TCP, and HTTPS requests. You can pass another hostname for TLS error investigation, for example `expired.badssl.com`; that request requires Internet access.

For a bounded, direct IPv4/IPv6 observation of the Steam Store (DNS, TCP/TLS timing, and HTTP status kept separate), run:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\observe_store_tls.ps1
```

The script performs at most three attempts per resolved address and timeout, pins requests to the DNS result, keeps Schannel certificate verification enabled, and writes JSON under ignored `out/`.

Configuration is stored under the current user's application config directory and validated on load. Only timeout and concurrency can be configured; the target hostnames stay fixed in the Steam registry. Diagnostics do not require administrator rights and do not change system DNS, proxy, hosts file, firewall, or packet traffic.

## Current scope

- Fixed Steam web target registry; no IP-range, UDP, or game-server scanning.
- DNS A/AAAA, dual-stack TCP/443, and WinHTTP HTTPS with Windows certificate and hostname validation.
- Bounded host concurrency, cancellation, stage-specific UI tabs, timestamps, and structured JSON report export.
- Read-only FLOW/SOCKET observation with Valve-signed Steam process identity checks; only correlated, verified Steam flows contribute to observational counters.
- DPI Start/Stop controls remain disabled. WinDivert is used only for explicit read-only FLOW/SOCKET observation; no NETWORK capture or packet manipulation is implemented.

Packet Observation is separate from DPI controls. To start it, launch SteamDPI explicitly with Administrator permission; the application does not elevate itself. The adapter uses FLOW/SOCKET event metadata only and stores no packet or TLS payload. It reports flow counts, not packet or byte totals. Unknown or ambiguous process/flow correlations are excluded from Steam metrics.

The pinned WinDivert 2.2.1 x64 DLL and driver are copied beside the built executable. A standalone package made with `windeployqt` must also include `WinDivert.dll`, `WinDivert64.sys`, and `WinDivert-LICENSE.txt` from the matching build configuration. See [dependency provenance](third_party/windivert/2.2.1/PROVENANCE.md).

See [architecture](docs/ARCHITECTURE.md), [roadmap](docs/ROADMAP.md), the [Phase 0–1 final acceptance report](docs/PHASE-0-1-REPORT.md), and the [Phase 2A acceptance report](docs/PHASE-2A-REPORT.md).
