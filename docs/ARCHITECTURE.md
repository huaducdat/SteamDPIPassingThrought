# Architecture

`AppController` owns configuration and UI-facing run state. It starts a bounded set of worker threads through `DiagnosticService`; results are returned as structured `ProbeResult` records and progress is marshalled to the Qt UI thread. Cancellation is shared through an atomic flag. Diagnostics operate on the fixed `SteamTargets` registry and never infer process identity from a hostname or port.

The DNS probe uses the Windows asynchronous resolver API and applies a finite wait. TCP uses nonblocking Winsock connect across IPv4 and IPv6 results. TLS uses WinHTTP HTTPS requests, which applies Windows certificate-chain and hostname validation; secure validation errors are surfaced distinctly. WinHTTP and socket timeouts are bounded by the configured value.

The UI is not intended to claim protection or bypass status. Engine state remains `NOT_IMPLEMENTED`. Diagnostic failures can identify DNS, connection, timeout, or certificate trouble, but a single timeout cannot establish DPI interference.

## Phase 2A read-only observation

`CaptureController` owns capture lifecycle and a bounded event queue. `WinDivertAdapter` dynamically loads the pinned WinDivert 2.2.1 x64 runtime, opens only FLOW and SOCKET layers with `WINDIVERT_FLAG_SNIFF | WINDIVERT_FLAG_RECV_ONLY`, and receives event metadata with a null packet buffer. It has no NETWORK-layer handle and makes no send, transform, drop, or reinjection call. FLOW and SOCKET event readers stop through `WinDivertShutdown` and are joined before handles are closed.

`ProcessIdentityResolver` reads Steam install roots from the Windows registry, resolves a process image path, verifies Authenticode trust and a Valve organization name, then restricts attribution to exact Steam executable identities beneath those roots. PID process-creation time is included to protect correlations from PID reuse. `SteamTrafficClassifier` requires both FLOW and SOCKET evidence with matching endpoint/parent IDs, process identity, and complete 5-tuple. Unknown, missing, or mismatched events are excluded. FLOW/SOCKET metadata counts describe observed flows, not packets, bytes, or network packet loss.

The bounded queue and flow table expose their own monitoring-event overflow counters. WinDivert does not expose a reliable packet-loss statistic through these event layers, so SteamDPI does not label these counters as packet loss. No payload or TLS content is retained. Driver startup is explicit and requires the process to already be elevated; the application never prompts for or performs elevation. Closing the UI stops capture and joins receive workers.

The selected release files and provenance are recorded in [`third_party/windivert/2.2.1/PROVENANCE.md`](../third_party/windivert/2.2.1/PROVENANCE.md). Official WinDivert documentation identifies FLOW as process-attributed flow lifecycle events, SOCKET as process-attributed socket-operation events, and NETWORK as a layer without PID data. The later DPI engine needs separate design and authorization; observation metrics do not implement or imply bypass behavior.
