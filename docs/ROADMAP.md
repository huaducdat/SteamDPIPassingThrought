# Roadmap

## Phase 0 — Foundation

C++20/CMake project, Qt Widgets shell, validated local configuration, basic nonsensitive diagnostic logging, and CTest foundation. The engine stays `NOT_IMPLEMENTED`.

## Phase 1 — Diagnostics

Fixed Steam hostname registry; bounded asynchronous orchestration; DNS A/AAAA, nonblocking TCP/443 for IPv4 and IPv6, validated HTTPS/TLS, cancellation, structured results, and report export. No system network settings are changed and ordinary diagnostics do not require administrator rights.

## Phase 2A — Safe observation

Implemented read-only WinDivert FLOW/SOCKET observation, verified Steam executable identity, conservative process-to-flow correlation, bounded queues, metrics, and lifecycle controls. Live driver validation and Steam connectivity comparison remain outstanding; see the [Phase 2A report](PHASE-2A-REPORT.md). This phase does not capture NETWORK packets or inspect payloads.

## Later work

Any DPI bypass or packet-processing engine requires separate scope, review, and authorization. It is not implemented by the current roadmap phase.
