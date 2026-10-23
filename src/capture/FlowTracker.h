#pragma once

#include "CaptureTypes.h"
#include "SteamTrafficClassifier.h"
#include "packet/PacketMetadata.h"
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <unordered_map>

namespace steamdpi {

enum class NetworkFlowMatch { MatchedSteam, Unmatched, Ambiguous };
enum class NetworkMatchReason {
  Matched, NoCorrespondingFlow, ProtocolMismatch, PortMismatch,
  FamilyMismatch, AddressMismatch, DirectionMismatch, BeforeFlowWindow,
  ExpiredFlow, AfterFlowClosed, UnverifiedIdentity, Ambiguous,
  LocalPortMismatch, RemotePortMismatch, LocalAddressMismatch, RemoteAddressMismatch
};
struct NetworkMatchResult {
  NetworkFlowMatch match = NetworkFlowMatch::Unmatched;
  NetworkMatchReason reason = NetworkMatchReason::NoCorrespondingFlow;
  std::uint64_t protocolCandidates = 0, portCandidates = 0, addressCandidates = 0, directionMismatches = 0;
  std::uint64_t familyCandidates = 0, steamProtocolCandidates = 0;
  std::uint64_t steamPortCandidates = 0, steamAddressCandidates = 0;
  std::uint64_t steamLocalAddressCandidates = 0, steamRemoteAddressCandidates = 0;
  std::uint64_t steamWordSwappedAddressCandidates = 0;
};

class FlowTracker {
public:
  explicit FlowTracker(std::size_t maximumTracked = 4096);
  void process(const CaptureEvent& event,
               const std::optional<ProcessIdentity>& identity);
  CaptureMetrics snapshot() const;
  NetworkFlowMatch matchNetworkPacket(const packet::PacketMetadata& metadata,
                                      bool outbound, std::uint64_t timestampNs);
  NetworkMatchResult diagnoseNetworkPacket(const packet::PacketMetadata& metadata,
                                           bool outbound, std::uint64_t timestampNs);
  void clear();
  void finish();
  std::vector<VerifiedFlowPortScope> verifiedSteamPortScopes() const;

private:
  struct Record {
    std::optional<CaptureEvent> flow;
    std::optional<CaptureEvent> socket;
    std::optional<ProcessIdentity> flowIdentity;
    std::optional<ProcessIdentity> socketIdentity;
    bool counted = false;
    SteamExecutableClass executable = SteamExecutableClass::UnknownOrAmbiguous;
    std::uint64_t flowTimestampNs = 0;
    std::uint64_t socketTimestampNs = 0;
    std::uint64_t closedTimestampNs = 0;
    bool active = true;
  };

  void tryCount(Record& record);
  void erase(std::uint64_t endpointId);
  void markClosed(const CaptureEvent& event);

  const std::size_t maximumTracked_;
  mutable std::mutex mutex_;
  std::unordered_map<std::uint64_t, Record> records_;
  CaptureMetrics metrics_;
  SteamTrafficClassifier classifier_;
};

} // namespace steamdpi
