#include "FlowTracker.h"
#include <array>
#include <limits>

namespace steamdpi {
namespace {
constexpr std::uint64_t kFlowExpirationNs = 60ull * 60ull * 1000000000ull;
// FLOW_ESTABLISHED is generated from the first packet. Permit small timestamp
// inversions between that event and the NETWORK event carrying the same packet.
constexpr std::uint64_t kFlowStartSkewNs = 500ull * 1000000ull;
std::array<std::uint8_t,16> packetAddress(const std::array<std::uint8_t,16>& address,bool ipv6) {
  if(ipv6)return address;
  std::array<std::uint8_t,16> mapped{};mapped[10]=0xff;mapped[11]=0xff;
  std::copy_n(address.begin(),4,mapped.begin()+12);return mapped;
}
std::array<std::uint8_t,16> reverseAddressWords(const std::array<std::uint8_t,16>& address) {
  auto reversed=address;
  for(std::size_t word=0;word<4;++word)
    std::reverse(reversed.begin()+static_cast<std::ptrdiff_t>(word*4),
                 reversed.begin()+static_cast<std::ptrdiff_t>(word*4+4));
  return reversed;
}
bool samePacketTuple(const FlowTuple& tuple,const packet::PacketMetadata& m,bool outbound) {
  if(tuple.protocol!=static_cast<std::uint8_t>(m.protocol)||tuple.ipv6!=(m.ipVersion==packet::IpVersion::V6))return false;
  const bool ipv6=m.ipVersion==packet::IpVersion::V6;
  const auto src=packetAddress(m.sourceAddress,ipv6),dst=packetAddress(m.destinationAddress,ipv6);
  if(outbound)return tuple.localAddress==src&&tuple.remoteAddress==dst&&tuple.localPort==m.sourcePort&&tuple.remotePort==m.destinationPort;
  return tuple.localAddress==dst&&tuple.remoteAddress==src&&tuple.localPort==m.destinationPort&&tuple.remotePort==m.sourcePort;
}
bool isVerifiedSteam(SteamExecutableClass value) {
  return value==SteamExecutableClass::SteamClient||value==SteamExecutableClass::SteamWebHelper;
}
}

FlowTracker::FlowTracker(std::size_t maximumTracked) : maximumTracked_(maximumTracked) {}

void FlowTracker::process(const CaptureEvent& event,
                          const std::optional<ProcessIdentity>& identity) {
  std::lock_guard lock(mutex_);
  if (event.kind == CaptureEventKind::Error) {
    ++metrics_.observationErrors;
    return;
  }
  if(event.layer==CaptureLayer::Flow)++metrics_.flowEventsObserved;
  else if(event.layer==CaptureLayer::Socket)++metrics_.socketEventsObserved;
  if (event.kind == CaptureEventKind::Deleted || event.kind == CaptureEventKind::Close) {
    markClosed(event);
    return;
  }
  if (event.kind == CaptureEventKind::Other || event.endpointId == 0 ||
      (event.tuple.protocol != 6 && event.tuple.protocol != 17))
    return;
  auto it = records_.find(event.endpointId);
  if (it == records_.end()) {
    if (records_.size() >= maximumTracked_) {
      ++metrics_.droppedMonitorEvents;
      return;
    }
    it = records_.try_emplace(event.endpointId).first;
  }
  auto& record = it->second;
  if(record.closedTimestampNs&&event.timestampNs>record.closedTimestampNs) {
    record=Record{};
  }
  if (event.layer == CaptureLayer::Flow && event.kind == CaptureEventKind::Established) {
    if(record.flow&&(record.flow->processId!=event.processId||record.flow->tuple!=event.tuple||
       record.flow->parentEndpointId!=event.parentEndpointId||
       (record.flowIdentity&&identity&&record.flowIdentity->processCreationTime!=identity->processCreationTime))) {
      if(record.counted&&record.active&&isVerifiedSteam(record.executable)&&metrics_.activeSteamFlows>0)--metrics_.activeSteamFlows;
      record=Record{};
    }
    record.flow = event;
    record.flowIdentity = identity;
    record.flowTimestampNs=event.timestampNs;
  } else if (event.layer == CaptureLayer::Socket &&
             (event.kind == CaptureEventKind::Connect || event.kind == CaptureEventKind::Accept)) {
    if(record.socket&&(record.socket->processId!=event.processId||record.socket->tuple!=event.tuple||
       record.socket->parentEndpointId!=event.parentEndpointId||
       (record.socketIdentity&&identity&&record.socketIdentity->processCreationTime!=identity->processCreationTime))) {
      if(record.counted&&record.active&&isVerifiedSteam(record.executable)&&metrics_.activeSteamFlows>0)--metrics_.activeSteamFlows;
      record=Record{};
    }
    record.socket = event;
    record.socketIdentity = identity;
    record.socketTimestampNs=event.timestampNs;
  }
  tryCount(record);
}

void FlowTracker::tryCount(Record& record) {
  if (record.counted || !record.flow || !record.socket) return;
  const auto executable = classifier_.classifyFlow(*record.flow, *record.socket,
      record.flowIdentity, record.socketIdentity);
  record.counted = true;
  record.executable=executable;
  if (executable == SteamExecutableClass::UnknownOrAmbiguous) {
    ++metrics_.unknownOrAmbiguousFlows;
    return;
  }
  ++metrics_.steamFlowsObserved;
  if(record.active)++metrics_.activeSteamFlows;
  if (executable == SteamExecutableClass::SteamClient) ++metrics_.steamExeFlows;
  if (executable == SteamExecutableClass::SteamWebHelper) ++metrics_.steamWebHelperFlows;
  if (record.flow->tuple.protocol == 6) ++metrics_.tcpFlows;
  if (record.flow->tuple.protocol == 17) ++metrics_.udpFlows;
  if (record.flow->tuple.ipv6) ++metrics_.ipv6Flows;
  else ++metrics_.ipv4Flows;
}

NetworkFlowMatch FlowTracker::matchNetworkPacket(const packet::PacketMetadata& metadata,
                                                bool outbound,std::uint64_t timestampNs) {
  return diagnoseNetworkPacket(metadata,outbound,timestampNs).match;
}

NetworkMatchResult FlowTracker::diagnoseNetworkPacket(const packet::PacketMetadata& metadata,
                                                       bool outbound,std::uint64_t timestampNs) {
  std::lock_guard lock(mutex_);
  bool anyFlow=false, familyCandidate=false, protocolCandidate=false, portCandidate=false;
  bool addressCandidate=false, directionMismatch=false, tooEarly=false, expired=false;
  bool verifiedMatch=false, identityUnverified=false, ambiguousMatch=false, afterClose=false;
  bool steamLocalPortCandidate=false,steamRemotePortCandidate=false,steamOppositeDirectionTuple=false;
  bool steamLocalAddressCandidate=false,steamRemoteAddressCandidate=false;
  std::uint64_t protocolCount=0,portCount=0,addressCount=0,directionCount=0,familyCount=0;
  std::uint64_t steamProtocolCount=0,steamPortCount=0,steamAddressCount=0;
  std::uint64_t steamLocalAddressCount=0,steamRemoteAddressCount=0;
  std::uint64_t steamWordSwappedAddressCount=0;
  const auto result=[&](NetworkFlowMatch match,NetworkMatchReason reason) {
    return NetworkMatchResult{match,reason,protocolCount,portCount,addressCount,directionCount,
                              familyCount,steamProtocolCount,steamPortCount,steamAddressCount,
                              steamLocalAddressCount,steamRemoteAddressCount,steamWordSwappedAddressCount};
  };
  for(auto it=records_.begin();it!=records_.end();) {
    auto& record=it->second;
    const auto start=std::max(record.flowTimestampNs,record.socketTimestampNs);
    const bool pastClose=record.closedTimestampNs&&timestampNs>record.closedTimestampNs;
    const bool closeExpired=record.closedTimestampNs&&timestampNs>record.closedTimestampNs&&
                            timestampNs-record.closedTimestampNs>kFlowStartSkewNs;
    if(closeExpired||(start&&timestampNs>start&&timestampNs-start>kFlowExpirationNs)) {
      expired=true;
      if(record.counted&&record.active&&isVerifiedSteam(record.executable)&&metrics_.activeSteamFlows>0)--metrics_.activeSteamFlows;
      it=records_.erase(it);continue;
    }
    ++it;
    if(!record.flow||!record.socket||!record.counted)continue;
    anyFlow=true;
    const auto& tuple=record.flow->tuple;
    const bool sameFamily=tuple.ipv6==(metadata.ipVersion==packet::IpVersion::V6);
    const bool sameProtocol=tuple.protocol==static_cast<std::uint8_t>(metadata.protocol);
    const bool verifiedSteam=isVerifiedSteam(record.executable);
    if(sameFamily){familyCandidate=true;++familyCount;}
    if(sameFamily&&sameProtocol) {
      protocolCandidate=true;++protocolCount;
      if(verifiedSteam)++steamProtocolCount;
    }
    const bool portOut=tuple.localPort==metadata.sourcePort&&tuple.remotePort==metadata.destinationPort;
    const bool portIn=tuple.localPort==metadata.destinationPort&&tuple.remotePort==metadata.sourcePort;
    if(sameFamily&&sameProtocol&&(portOut||portIn)) {portCandidate=true;++portCount;if(verifiedSteam)++steamPortCount;}
    const auto src=packetAddress(metadata.sourceAddress,metadata.ipVersion==packet::IpVersion::V6);
    const auto dst=packetAddress(metadata.destinationAddress,metadata.ipVersion==packet::IpVersion::V6);
    const bool addrOut=tuple.localAddress==src&&tuple.remoteAddress==dst;
    const bool addrIn=tuple.localAddress==dst&&tuple.remoteAddress==src;
    const bool swappedAddrOut=reverseAddressWords(tuple.localAddress)==src&&reverseAddressWords(tuple.remoteAddress)==dst;
    const bool swappedAddrIn=reverseAddressWords(tuple.localAddress)==dst&&reverseAddressWords(tuple.remoteAddress)==src;
    if(verifiedSteam&&sameFamily&&sameProtocol&&(portOut||portIn)) {
      if(tuple.localAddress==src||tuple.localAddress==dst)++steamLocalAddressCount;
      if(tuple.remoteAddress==src||tuple.remoteAddress==dst)++steamRemoteAddressCount;
    }
    if(verifiedSteam&&sameFamily&&sameProtocol) {
      const auto expectedLocalPort=outbound?metadata.sourcePort:metadata.destinationPort;
      const auto expectedRemotePort=outbound?metadata.destinationPort:metadata.sourcePort;
      const bool localPortMatch=tuple.localPort==expectedLocalPort;
      const bool remotePortMatch=tuple.remotePort==expectedRemotePort;
      steamLocalPortCandidate|=localPortMatch;
      steamRemotePortCandidate|=remotePortMatch;
      steamOppositeDirectionTuple|=outbound?portIn:portOut;
      const auto& expectedLocalAddress=outbound?src:dst;
      const auto& expectedRemoteAddress=outbound?dst:src;
      steamLocalAddressCandidate|=tuple.localAddress==expectedLocalAddress;
      steamRemoteAddressCandidate|=tuple.remoteAddress==expectedRemoteAddress;
    }
    if(sameFamily&&sameProtocol&&(portOut||portIn)&&(addrOut||addrIn)) {addressCandidate=true;++addressCount;if(verifiedSteam)++steamAddressCount;}
    if(sameFamily&&sameProtocol&&(portOut||portIn)&&(swappedAddrOut||swappedAddrIn)&&verifiedSteam)++steamWordSwappedAddressCount;
    if(sameFamily&&sameProtocol&&((outbound&&!addrOut&&addrIn)||(!outbound&&!addrIn&&addrOut))) {directionMismatch=true;++directionCount;}
    if(!samePacketTuple(tuple,metadata,outbound))continue;
    const auto first=std::min(record.flowTimestampNs,record.socketTimestampNs);
    if(first&&timestampNs<first&&first-timestampNs>kFlowStartSkewNs) {tooEarly=true;continue;}
    if(pastClose) {afterClose=true;continue;}
    if(record.executable==SteamExecutableClass::UnknownOrAmbiguous) {identityUnverified=true;continue;}
    if(verifiedMatch)ambiguousMatch=true;
    verifiedMatch=true;
  }
  if(ambiguousMatch)return result(NetworkFlowMatch::Ambiguous,NetworkMatchReason::Ambiguous);
  if(verifiedMatch)return result(NetworkFlowMatch::MatchedSteam,NetworkMatchReason::Matched);
  if(identityUnverified)return result(NetworkFlowMatch::Ambiguous,NetworkMatchReason::UnverifiedIdentity);
  if(tooEarly)return result(NetworkFlowMatch::Unmatched,NetworkMatchReason::BeforeFlowWindow);
  if(afterClose)return result(NetworkFlowMatch::Unmatched,NetworkMatchReason::AfterFlowClosed);
  if(directionMismatch)return result(NetworkFlowMatch::Unmatched,NetworkMatchReason::DirectionMismatch);
  if(!anyFlow)return result(NetworkFlowMatch::Unmatched,expired?NetworkMatchReason::ExpiredFlow:NetworkMatchReason::NoCorrespondingFlow);
  if(!familyCandidate)return result(NetworkFlowMatch::Unmatched,NetworkMatchReason::FamilyMismatch);
  if(!protocolCandidate)return result(NetworkFlowMatch::Unmatched,NetworkMatchReason::ProtocolMismatch);
  if(!portCandidate) {
    if(steamOppositeDirectionTuple)return result(NetworkFlowMatch::Unmatched,NetworkMatchReason::DirectionMismatch);
    if(steamLocalPortCandidate&&!steamRemotePortCandidate)return result(NetworkFlowMatch::Unmatched,NetworkMatchReason::RemotePortMismatch);
    if(steamRemotePortCandidate&&!steamLocalPortCandidate)return result(NetworkFlowMatch::Unmatched,NetworkMatchReason::LocalPortMismatch);
    return result(NetworkFlowMatch::Unmatched,NetworkMatchReason::PortMismatch);
  }
  if(!addressCandidate) {
    if(steamLocalAddressCandidate&&!steamRemoteAddressCandidate)return result(NetworkFlowMatch::Unmatched,NetworkMatchReason::RemoteAddressMismatch);
    if(steamRemoteAddressCandidate&&!steamLocalAddressCandidate)return result(NetworkFlowMatch::Unmatched,NetworkMatchReason::LocalAddressMismatch);
    if(!steamLocalAddressCandidate)return result(NetworkFlowMatch::Unmatched,NetworkMatchReason::LocalAddressMismatch);
    if(!steamRemoteAddressCandidate)return result(NetworkFlowMatch::Unmatched,NetworkMatchReason::RemoteAddressMismatch);
    return result(NetworkFlowMatch::Unmatched,NetworkMatchReason::AddressMismatch);
  }
  return result(NetworkFlowMatch::Unmatched,NetworkMatchReason::DirectionMismatch);
}

void FlowTracker::erase(std::uint64_t endpointId) {
  const auto it = records_.find(endpointId);
  if (it == records_.end()) return;
  if (it->second.counted && it->second.active && isVerifiedSteam(it->second.executable) &&
      metrics_.activeSteamFlows > 0) --metrics_.activeSteamFlows;
  records_.erase(it);
}

void FlowTracker::markClosed(const CaptureEvent& event) {
  const auto it=records_.find(event.endpointId);
  if(it==records_.end())return;
  auto& record=it->second;
  if(!event.timestampNs){erase(event.endpointId);return;}
  if(!record.closedTimestampNs||event.timestampNs>record.closedTimestampNs)
    record.closedTimestampNs=event.timestampNs;
  if(record.active) {
    if(record.counted&&isVerifiedSteam(record.executable)&&metrics_.activeSteamFlows>0)--metrics_.activeSteamFlows;
    record.active=false;
  }
}

CaptureMetrics FlowTracker::snapshot() const {
  std::lock_guard lock(mutex_);
  return metrics_;
}

void FlowTracker::clear() {
  std::lock_guard lock(mutex_);
  records_.clear();
  metrics_ = {};
}

void FlowTracker::finish() {
  std::lock_guard lock(mutex_);
  records_.clear();
  metrics_.activeSteamFlows = 0;
}

std::vector<VerifiedFlowPortScope> FlowTracker::verifiedSteamPortScopes() const {
  std::lock_guard lock(mutex_);
  std::vector<VerifiedFlowPortScope> result;
  result.reserve(records_.size());
  for(const auto& [id,record]:records_) {
    (void)id;
    if(!record.counted||!isVerifiedSteam(record.executable)||!record.flow||!record.socket)continue;
    const auto& tuple=record.flow->tuple;
    result.push_back({tuple.localPort,tuple.remotePort,tuple.protocol,tuple.ipv6,
                      std::min(record.flowTimestampNs,record.socketTimestampNs),record.closedTimestampNs});
  }
  return result;
}

} // namespace steamdpi
