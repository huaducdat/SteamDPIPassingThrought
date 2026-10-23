# WinDivert 2.2.1 x64 provenance

- Upstream project: <https://github.com/basil00/WinDivert>
- Pinned release: `v2.2.1`, asset `WinDivert-2.2.1-A.zip`
- Official asset: <https://github.com/basil00/WinDivert/releases/download/v2.2.1/WinDivert-2.2.1-A.zip>
- Downloaded archive SHA-256: `CF78031E9DE3948E65A7D1EF197DBC3C9D4D67C5AC6A220F3B8A1C540B5E5DFD`
- Included upstream files only: x64 `WinDivert.dll`, `WinDivert64.sys`, public header, version, and license. The DLL is loaded dynamically, so the import library is unnecessary. Upstream sample executables and x86 binaries are not included.
- `WinDivert64.sys` Authenticode status on 2026-10-22: Valid; signer certificate thumbprint `043589F75FCE2795E7F2CC3E526D46784D5DDAB3`.
- `WinDivert.dll` has no embedded Authenticode signature. Its pinned file SHA-256 is `4B0BDD875D02DF084D87D1573BCECF393D67DA4AB3DDCCE725AF2F477B90D87E`; the driver SHA-256 is `8248306BCC5FAE20FD4F3D5C44F962C85CDDBE020B34A1799350CE2034154B7D`.

The archive was downloaded directly from the upstream GitHub release over HTTPS. The release assets do not publish an independent SHA-256 digest, so the recorded archive digest pins the exact bytes inspected here; the driver's Authenticode signature provides an additional publisher-integrity check. The project license from the release is retained alongside the selected files.
