#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace steamdpi {

enum class CaptureLayer { Flow, Socket, Network };
enum class CaptureEventKind { Established, Deleted, Connect, Accept, Close, NetworkPacket, Other, Error };
enum class CaptureState { Stopped, Starting, Capturing, Error };
enum class NetworkCaptureProfile { AllTcpUdp, VerifiedSteamEndpointCandidates };

struct FlowTuple {
  // Canonical network-order bytes. WinDivert FLOW/SOCKET IPv4 endpoints are
  // normalized to IPv4-mapped IPv6 form by the adapter.
  std::array<std::uint8_t, 16> localAddress{};
  std::array<std::uint8_t, 16> remoteAddress{};
  std::uint16_t localPort = 0;
  std::uint16_t remotePort = 0;
  std::uint8_t protocol = 0;
  bool ipv6 = false;

  bool operator==(const FlowTuple&) const = default;
};

struct VerifiedFlowPortScope {
  std::uint16_t localPort=0,remotePort=0;
  std::uint8_t protocol=0;
  bool ipv6=false;
  std::uint64_t firstTimestampNs=0,closedTimestampNs=0;
};

struct CaptureEvent {
  CaptureLayer layer = CaptureLayer::Flow;
  CaptureEventKind kind = CaptureEventKind::Established;
  std::uint64_t endpointId = 0;
  std::uint64_t parentEndpointId = 0;
  std::uint32_t processId = 0;
  FlowTuple tuple;
  std::uint32_t error = 0;
  std::uint64_t timestampNs = 0;
  std::uint64_t captureLatencyNs = 0;
  bool outbound = false;
  bool loopback = false;
  bool ipv4ChecksumValid = false;
  bool transportChecksumValid = false;
  std::vector<std::uint8_t> packetBytes;
};

struct ProcessIdentity {
  std::uint32_t processId = 0;
  std::wstring imagePath;
  bool signatureVerified = false;
  std::uint64_t processCreationTime = 0;
};

struct CaptureMetrics {
  std::uint64_t steamFlowsObserved = 0;
  std::uint64_t steamExeFlows = 0;
  std::uint64_t steamWebHelperFlows = 0;
  std::uint64_t unknownOrAmbiguousFlows = 0;
  std::uint64_t tcpFlows = 0;
  std::uint64_t udpFlows = 0;
  std::uint64_t ipv4Flows = 0;
  std::uint64_t ipv6Flows = 0;
  std::uint64_t observationErrors = 0;
  std::uint64_t droppedMonitorEvents = 0;
  std::uint64_t activeSteamFlows = 0;
  std::uint64_t flowEventsObserved = 0;
  std::uint64_t socketEventsObserved = 0;
  std::uint64_t networkPacketsObserved = 0;
  std::uint64_t networkPacketsFilteredBeforeParser = 0;
  std::uint64_t networkPacketsDiscardedInactiveState = 0;
  std::uint64_t networkFilteredNoVerifiedFlow = 0;
  std::uint64_t networkFilteredOutsideEndpointScope = 0;
  std::uint64_t networkPacketsParsed = 0;
  std::uint64_t networkPacketsRejected = 0;
  std::uint64_t networkRejectedChecksum = 0;
  std::uint64_t networkRejectedTruncated = 0;
  std::uint64_t networkRejectedUnsupported = 0;
  std::uint64_t networkRejectedLength = 0;
  std::uint64_t networkRejectedOther = 0;
  std::uint64_t networkPacketsChecksumUnverified = 0;
  std::uint64_t networkIpv4Packets = 0;
  std::uint64_t networkIpv6Packets = 0;
  std::uint64_t networkPacketsMatchedSteam = 0;
  std::uint64_t networkPacketsUnmatched = 0;
  std::uint64_t networkPacketsAmbiguous = 0;
  std::uint64_t networkNoCorrespondingFlow = 0;
  std::uint64_t networkProtocolMismatch = 0;
  std::uint64_t networkPortMismatch = 0;
  std::uint64_t networkLocalPortMismatch = 0;
  std::uint64_t networkRemotePortMismatch = 0;
  std::uint64_t networkAddressMismatch = 0;
  std::uint64_t networkLocalAddressMismatch = 0;
  std::uint64_t networkRemoteAddressMismatch = 0;
  std::uint64_t networkOutsideAttributionWindow = 0;
  std::uint64_t networkDirectionMismatch = 0;
  std::uint64_t networkBeforeFlowWindow = 0;
  std::uint64_t networkExpiredFlow = 0;
  std::uint64_t networkAfterFlowClosed = 0;
  std::uint64_t networkFamilyMismatch = 0;
  std::uint64_t networkUnverifiedIdentity = 0;
  std::uint64_t networkCorrelationQueueDrops = 0;
  std::uint64_t networkCorrelationQueueHighWater = 0;
  std::uint64_t networkQueueHighWater = 0;
  std::uint64_t flowSocketQueueHighWater = 0;
  std::uint64_t networkEventsEnqueued = 0, networkEventsDequeued = 0;
  std::uint64_t flowSocketEventsEnqueued = 0, flowSocketEventsDequeued = 0;
  std::uint64_t networkCorrelationCandidatesQueued = 0;
  std::uint64_t networkCorrelationsFinalized = 0;
  std::uint64_t networkCaptureQueueDrops = 0;
  std::uint64_t flowSocketEventQueueDrops = 0;
  std::uint64_t networkFlowProtocolCandidates = 0;
  std::uint64_t networkFlowPortCandidates = 0;
  std::uint64_t networkFlowAddressCandidates = 0;
  std::uint64_t networkFlowDirectionMismatches = 0;
  std::uint64_t networkFlowFamilyCandidates = 0;
  std::uint64_t steamFlowProtocolCandidates = 0;
  std::uint64_t steamFlowPortCandidates = 0;
  std::uint64_t steamFlowAddressCandidates = 0;
  std::uint64_t steamFlowLocalAddressCandidates = 0;
  std::uint64_t steamFlowRemoteAddressCandidates = 0;
  std::uint64_t steamFlowWordSwappedAddressCandidates = 0;
  std::uint64_t networkBytesObserved = 0;
  std::uint64_t captureLatencyTotalNs = 0;
  std::uint64_t captureLatencyMaxNs = 0;
  std::uint64_t parseLatencyTotalNs = 0;
  std::uint64_t parseLatencyMaxNs = 0;
  std::uint64_t correlationLatencyTotalNs = 0;
  std::uint64_t correlationLatencyMaxNs = 0;
};

struct CaptureSnapshot {
  CaptureState state = CaptureState::Stopped;
  CaptureMetrics metrics;
  std::uint32_t lastError = 0;
  std::string status;
};

} // namespace steamdpi
