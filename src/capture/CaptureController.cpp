#include "CaptureController.h"
#include "ProcessIdentityResolver.h"
#include "WinDivertAdapter.h"
#include <windows.h>
#include <algorithm>
#include <chrono>
#include <utility>
#include <iterator>

namespace {
struct L4HeaderPeek { bool valid=false; bool malformed=false; bool ipv6=false; std::uint8_t protocol=0; std::uint16_t sourcePort=0,destinationPort=0; };
std::uint16_t readBe16(const std::vector<std::uint8_t>& bytes,std::size_t at) {
  return static_cast<std::uint16_t>((static_cast<std::uint16_t>(bytes[at])<<8)|bytes[at+1]);
}
L4HeaderPeek peekTransportTuple(const std::vector<std::uint8_t>& bytes) {
  L4HeaderPeek result;
  if(bytes.empty()) {result.malformed=true;return result;}
  const auto version=bytes[0]>>4;
  std::size_t offset=0;
  if(version==4) {
    if(bytes.size()<20) {result.malformed=true;return result;}
    offset=static_cast<std::size_t>(bytes[0]&0x0f)*4;
    if(offset<20||offset>bytes.size()) {result.malformed=true;return result;}
    result.protocol=bytes[9];
    if((readBe16(bytes,6)&0x3fffu)!=0) {result.malformed=true;return result;}
  } else if(version==6) {
    if(bytes.size()<40) {result.malformed=true;return result;}
    result.ipv6=true;offset=40;result.protocol=bytes[6];
    // Extension headers remain visible to the full parser for an explicit
    // unsupported classification; they are never guessed through here.
    if(result.protocol!=6&&result.protocol!=17) {result.malformed=true;return result;}
  } else {result.malformed=true;return result;}
  if(result.protocol!=6&&result.protocol!=17) return result;
  if(offset+4>bytes.size()) {result.malformed=true;return result;}
  result.sourcePort=readBe16(bytes,offset);result.destinationPort=readBe16(bytes,offset+2);result.valid=true;
  return result;
}
}

namespace steamdpi {

CaptureController::CaptureController(std::unique_ptr<CaptureAdapter> adapter,
                                     IdentityLookup identityLookup,
                                     bool requireAdministrator,
                                     NetworkCaptureProfile profile)
    : adapter_(adapter ? std::move(adapter) : std::make_unique<WinDivertAdapter>()),
      identityLookup_(identityLookup ? std::move(identityLookup)
                                     : IdentityLookup{[](std::uint32_t pid) {
                                         static ProcessIdentityResolver resolver;
                                         return resolver.resolve(pid);
                                       }}),
      requireAdministrator_(requireAdministrator),
      networkProfile_(profile) {
  pendingNetworkCorrelations_.reserve(kPendingCorrelationCapacity);
  verifiedSteamScopes_.store(std::make_shared<const std::vector<VerifiedFlowPortScope>>());
  processingThread_=std::thread([this] {
        SetThreadDescription(GetCurrentThread(), L"SteamDPI Capture Event Worker");
        processEvents();
      });
  networkProcessingThread_=std::thread([this] {
        SetThreadDescription(GetCurrentThread(), L"SteamDPI NETWORK Parse Worker");
        processNetworkEvents();
      });
}

CaptureController::~CaptureController() {
  shuttingDown_ = true;
  stop();
  events_.close();
  networkEvents_.close();
  if (processingThread_.joinable()) processingThread_.join();
  if (networkProcessingThread_.joinable()) networkProcessingThread_.join();
}

bool CaptureController::start(std::string& error) {
  std::lock_guard lifecycle(lifecycleMutex_);
  const auto current = snapshot();
  if (current.state == CaptureState::Capturing || current.state == CaptureState::Starting) {
    error = "Packet observation is already running";
    return false;
  }
  if (requireAdministrator_ && !isAdministrator()) {
    error = "Packet observation requires Administrator permission. Restart SteamDPI explicitly as Administrator; no elevation was requested.";
    setState(CaptureState::Error, error, ERROR_ELEVATION_REQUIRED);
    publish(true);
    return false;
  }
  events_.clear();
  networkEvents_.clear();
  flushCorrelations_ = false;
  tracker_.clear();
  refreshSteamCandidateScopes();
  { std::lock_guard lock(stateMutex_); networkMetrics_={}; }
  droppedEvents_ = 0;
  droppedNetworkEvents_ = 0;
  droppedFlowSocketEvents_ = 0;
  setState(CaptureState::Starting, "Opening read-only WinDivert observation handles");
  const bool started = adapter_->start(
      [this](CaptureEvent event) { enqueue(std::move(event)); },
      [this](std::uint32_t code) {
        CaptureEvent event{};
        event.kind = CaptureEventKind::Error;
        event.error = code;
        enqueue(std::move(event));
      }, error);
  if (!started) {
    const auto code = GetLastError();
    const auto closeError = adapter_->lastStopError();
    tracker_.process(CaptureEvent{.kind = CaptureEventKind::Error}, std::nullopt);
    if (closeError != 0) {
      setState(CaptureState::Error,
               error + "; partial-start handle cleanup failed (Win32 " +
                   std::to_string(closeError) + ")",
               closeError);
    } else {
      setState(CaptureState::Error, error, code);
    }
    publish(true);
    return false;
  }
  setState(CaptureState::Capturing, "Read-only FLOW/SOCKET and NETWORK packet observation active");
  publish(true);
  return true;
}

void CaptureController::stop() {
  std::lock_guard lifecycle(lifecycleMutex_);
  const auto current = snapshot();
  if (current.state == CaptureState::Stopped) return;
  {
    std::lock_guard lock(stateMutex_);
    snapshot_.status="Stopping observation handles";
  }
  publish(true);
  adapter_->stop();
  const auto closeError = adapter_->lastStopError();
  {
    std::unique_lock lock(drainMutex_);
    drainChanged_.wait_for(lock,std::chrono::milliseconds(1000),[this]{
      return eventOutstanding_.load()==0;
    });
  }
  flushCorrelations_=true;
  {
    std::unique_lock lock(drainMutex_);
    drainChanged_.notify_all();
    drainChanged_.wait_for(lock,std::chrono::milliseconds(1000),[this]{return !flushCorrelations_.load();});
  }
  tracker_.finish();
  if (closeError != 0) {
    setState(CaptureState::Error,
             "WinDivert handle shutdown failed (Win32 " + std::to_string(closeError) + ")",
             closeError);
  } else {
    setState(CaptureState::Stopped, "Observation stopped");
  }
  publish(true);
}

CaptureSnapshot CaptureController::snapshot() const {
  std::lock_guard lock(stateMutex_);
  CaptureSnapshot result = snapshot_;
  result.metrics = tracker_.snapshot();
  result.metrics.networkPacketsObserved=networkMetrics_.networkPacketsObserved;
  result.metrics.networkPacketsFilteredBeforeParser=networkMetrics_.networkPacketsFilteredBeforeParser;
  result.metrics.networkPacketsDiscardedInactiveState=networkMetrics_.networkPacketsDiscardedInactiveState;
  result.metrics.networkFilteredNoVerifiedFlow=networkMetrics_.networkFilteredNoVerifiedFlow;
  result.metrics.networkFilteredOutsideEndpointScope=networkMetrics_.networkFilteredOutsideEndpointScope;
  result.metrics.networkPacketsParsed=networkMetrics_.networkPacketsParsed;
  result.metrics.networkPacketsRejected=networkMetrics_.networkPacketsRejected;
  result.metrics.networkRejectedChecksum=networkMetrics_.networkRejectedChecksum;
  result.metrics.networkRejectedTruncated=networkMetrics_.networkRejectedTruncated;
  result.metrics.networkRejectedUnsupported=networkMetrics_.networkRejectedUnsupported;
  result.metrics.networkRejectedLength=networkMetrics_.networkRejectedLength;
  result.metrics.networkRejectedOther=networkMetrics_.networkRejectedOther;
  result.metrics.networkPacketsChecksumUnverified=networkMetrics_.networkPacketsChecksumUnverified;
  result.metrics.networkIpv4Packets=networkMetrics_.networkIpv4Packets;
  result.metrics.networkIpv6Packets=networkMetrics_.networkIpv6Packets;
  result.metrics.networkPacketsMatchedSteam=networkMetrics_.networkPacketsMatchedSteam;
  result.metrics.networkPacketsUnmatched=networkMetrics_.networkPacketsUnmatched;
  result.metrics.networkPacketsAmbiguous=networkMetrics_.networkPacketsAmbiguous;
  result.metrics.networkNoCorrespondingFlow=networkMetrics_.networkNoCorrespondingFlow;
  result.metrics.networkProtocolMismatch=networkMetrics_.networkProtocolMismatch;
  result.metrics.networkPortMismatch=networkMetrics_.networkPortMismatch;
  result.metrics.networkAddressMismatch=networkMetrics_.networkAddressMismatch;
  result.metrics.networkLocalPortMismatch=networkMetrics_.networkLocalPortMismatch;
  result.metrics.networkRemotePortMismatch=networkMetrics_.networkRemotePortMismatch;
  result.metrics.networkLocalAddressMismatch=networkMetrics_.networkLocalAddressMismatch;
  result.metrics.networkRemoteAddressMismatch=networkMetrics_.networkRemoteAddressMismatch;
  result.metrics.networkOutsideAttributionWindow=networkMetrics_.networkOutsideAttributionWindow;
  result.metrics.networkDirectionMismatch=networkMetrics_.networkDirectionMismatch;
  result.metrics.networkFamilyMismatch=networkMetrics_.networkFamilyMismatch;
  result.metrics.networkBeforeFlowWindow=networkMetrics_.networkBeforeFlowWindow;
  result.metrics.networkExpiredFlow=networkMetrics_.networkExpiredFlow;
  result.metrics.networkAfterFlowClosed=networkMetrics_.networkAfterFlowClosed;
  result.metrics.networkUnverifiedIdentity=networkMetrics_.networkUnverifiedIdentity;
  result.metrics.networkCorrelationQueueDrops=networkMetrics_.networkCorrelationQueueDrops;
  result.metrics.networkCorrelationQueueHighWater=networkMetrics_.networkCorrelationQueueHighWater;
  result.metrics.networkCorrelationCandidatesQueued=networkMetrics_.networkCorrelationCandidatesQueued;
  result.metrics.networkCorrelationsFinalized=networkMetrics_.networkCorrelationsFinalized;
  result.metrics.networkQueueHighWater=networkEvents_.highWaterMark();
  result.metrics.flowSocketQueueHighWater=events_.highWaterMark();
  result.metrics.networkEventsEnqueued=networkEvents_.pushes();
  result.metrics.networkEventsDequeued=networkEvents_.pops();
  result.metrics.flowSocketEventsEnqueued=events_.pushes();
  result.metrics.flowSocketEventsDequeued=events_.pops();
  result.metrics.networkCaptureQueueDrops=droppedNetworkEvents_.load();
  result.metrics.flowSocketEventQueueDrops=droppedFlowSocketEvents_.load();
  result.metrics.networkFlowProtocolCandidates=networkMetrics_.networkFlowProtocolCandidates;
  result.metrics.networkFlowPortCandidates=networkMetrics_.networkFlowPortCandidates;
  result.metrics.networkFlowAddressCandidates=networkMetrics_.networkFlowAddressCandidates;
  result.metrics.networkFlowDirectionMismatches=networkMetrics_.networkFlowDirectionMismatches;
  result.metrics.networkFlowFamilyCandidates=networkMetrics_.networkFlowFamilyCandidates;
  result.metrics.steamFlowProtocolCandidates=networkMetrics_.steamFlowProtocolCandidates;
  result.metrics.steamFlowPortCandidates=networkMetrics_.steamFlowPortCandidates;
  result.metrics.steamFlowAddressCandidates=networkMetrics_.steamFlowAddressCandidates;
  result.metrics.steamFlowWordSwappedAddressCandidates=networkMetrics_.steamFlowWordSwappedAddressCandidates;
  result.metrics.networkBytesObserved=networkMetrics_.networkBytesObserved;
  result.metrics.captureLatencyTotalNs=networkMetrics_.captureLatencyTotalNs;
  result.metrics.captureLatencyMaxNs=networkMetrics_.captureLatencyMaxNs;
  result.metrics.parseLatencyTotalNs=networkMetrics_.parseLatencyTotalNs;
  result.metrics.parseLatencyMaxNs=networkMetrics_.parseLatencyMaxNs;
  result.metrics.correlationLatencyTotalNs=networkMetrics_.correlationLatencyTotalNs;
  result.metrics.correlationLatencyMaxNs=networkMetrics_.correlationLatencyMaxNs;
  result.metrics.droppedMonitorEvents += droppedEvents_.load();
  return result;
}

void CaptureController::setSnapshotCallback(SnapshotCallback callback) {
  std::lock_guard lock(stateMutex_);
  snapshotCallback_ = std::move(callback);
}

bool CaptureController::isAdministrator() {
  BOOL isAdmin = FALSE;
  SID_IDENTIFIER_AUTHORITY authority = SECURITY_NT_AUTHORITY;
  PSID administrators = nullptr;
  if (!AllocateAndInitializeSid(&authority, 2, SECURITY_BUILTIN_DOMAIN_RID,
      DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &administrators)) return false;
  CheckTokenMembership(nullptr, administrators, &isAdmin);
  FreeSid(administrators);
  return isAdmin != FALSE;
}

void CaptureController::processEvents() {
  while (const auto event = events_.waitPop()) {
    const auto completed=[this]{--eventOutstanding_;drainChanged_.notify_all();};
    if (event->kind == CaptureEventKind::Error) {
      tracker_.process(*event, std::nullopt);
      const auto current = snapshot();
      if (current.state == CaptureState::Capturing) {
        const auto code = event->error;
        {
          std::lock_guard lifecycle(lifecycleMutex_);
          if (snapshot().state == CaptureState::Capturing) {
            setState(CaptureState::Error,
                     "WinDivert observation stopped after receive error " + std::to_string(code),
                     code);
            adapter_->stop();
            tracker_.finish();
            const auto closeError = adapter_->lastStopError();
            if (closeError != 0) {
              setState(CaptureState::Error,
                       "WinDivert receive and handle shutdown failed (Win32 " +
                           std::to_string(closeError) + ")",
                       closeError);
            }
          }
        }
      }
      publish(true);
      completed();
      continue;
    }
    const auto eventState=snapshot().state;
    if (eventState != CaptureState::Capturing && eventState != CaptureState::Starting) {
      completed();continue;
    }
    const auto identity = identityLookup_(event->processId);
    tracker_.process(*event, identity);
    refreshSteamCandidateScopes();
    publish();
    completed();
  }
}

void CaptureController::processNetworkEvents() {
  for (;;) {
    auto event=networkEvents_.waitPopFor(std::chrono::milliseconds(25));
    if(event) processNetworkPacket(std::move(*event));
    const bool flush=flushCorrelations_.load()&&networkEvents_.size()==0;
    retryPendingNetworkPackets(flush);
    if(flush){flushCorrelations_=false;drainChanged_.notify_all();}
    if(!event&&shuttingDown_){retryPendingNetworkPackets(true);break;}
  }
}

void CaptureController::processNetworkPacket(CaptureEvent event) {
  const auto eventState=snapshot().state;
  if(eventState!=CaptureState::Capturing&&eventState!=CaptureState::Starting) {
    std::lock_guard lock(stateMutex_);
    ++networkMetrics_.networkPacketsDiscardedInactiveState;
    return;
  }
  using Clock=std::chrono::steady_clock;
  const auto start=Clock::now();
  packet::PacketParseOptions parseOptions;
  parseOptions.verifyIpv4HeaderChecksum=event.ipv4ChecksumValid;
  parseOptions.verifyTransportChecksum=event.transportChecksumValid;
  const auto parsed=packet::PacketParser{}.parse(packet::PacketBuffer(std::move(event.packetBytes)),parseOptions);
  const auto elapsed=static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-start).count());
  {
    std::lock_guard lock(stateMutex_);
    networkMetrics_.parseLatencyTotalNs+=elapsed;
    networkMetrics_.parseLatencyMaxNs=std::max(networkMetrics_.parseLatencyMaxNs,elapsed);
    if(parsed.valid){++networkMetrics_.networkPacketsParsed;
      if(parsed.metadata.ipVersion==packet::IpVersion::V4)++networkMetrics_.networkIpv4Packets;
      else ++networkMetrics_.networkIpv6Packets;
    }else {
      ++networkMetrics_.networkPacketsRejected;
      using packet::ParseError;
      switch(parsed.error) {
        case ParseError::InvalidChecksum: ++networkMetrics_.networkRejectedChecksum; break;
        case ParseError::Truncated: ++networkMetrics_.networkRejectedTruncated; break;
        case ParseError::UnsupportedFragment:
        case ParseError::UnsupportedExtensionHeader: ++networkMetrics_.networkRejectedUnsupported; break;
        case ParseError::InvalidLength:
        case ParseError::InvalidHeaderLength: ++networkMetrics_.networkRejectedLength; break;
        default: ++networkMetrics_.networkRejectedOther; break;
      }
    }
    if(parsed.valid&&!parsed.checksumsVerified)++networkMetrics_.networkPacketsChecksumUnverified;
  }
  if(parsed.valid) {
    // Metadata only (no packet bytes); keep the race-reconciliation window
    // bounded while tolerating short capture bursts.
    if(pendingNetworkCorrelations_.size()>=kPendingCorrelationCapacity) {
      std::lock_guard lock(stateMutex_);
      ++networkMetrics_.networkCorrelationQueueDrops;
    } else {
      pendingNetworkCorrelations_.push_back({parsed.metadata,event.outbound,event.timestampNs,Clock::now()});
      std::lock_guard lock(stateMutex_);
      ++networkMetrics_.networkCorrelationCandidatesQueued;
      networkMetrics_.networkCorrelationQueueHighWater=std::max<std::uint64_t>(
          networkMetrics_.networkCorrelationQueueHighWater,pendingNetworkCorrelations_.size());
    }
  }
  publish();
}

void CaptureController::accountNetworkMatch(const NetworkMatchResult& result) {
  std::lock_guard lock(stateMutex_);
  networkMetrics_.networkFlowProtocolCandidates+=result.protocolCandidates;
  networkMetrics_.networkFlowPortCandidates+=result.portCandidates;
  networkMetrics_.networkFlowAddressCandidates+=result.addressCandidates;
  networkMetrics_.networkFlowDirectionMismatches+=result.directionMismatches;
  networkMetrics_.networkFlowFamilyCandidates+=result.familyCandidates;
  networkMetrics_.steamFlowProtocolCandidates+=result.steamProtocolCandidates;
  networkMetrics_.steamFlowPortCandidates+=result.steamPortCandidates;
  networkMetrics_.steamFlowAddressCandidates+=result.steamAddressCandidates;
  networkMetrics_.steamFlowLocalAddressCandidates+=result.steamLocalAddressCandidates;
  networkMetrics_.steamFlowRemoteAddressCandidates+=result.steamRemoteAddressCandidates;
  networkMetrics_.steamFlowWordSwappedAddressCandidates+=result.steamWordSwappedAddressCandidates;
  if(result.match==NetworkFlowMatch::MatchedSteam) ++networkMetrics_.networkPacketsMatchedSteam;
  else if(result.match==NetworkFlowMatch::Ambiguous) ++networkMetrics_.networkPacketsAmbiguous;
  else ++networkMetrics_.networkPacketsUnmatched;
  switch(result.reason) {
    case NetworkMatchReason::NoCorrespondingFlow: ++networkMetrics_.networkNoCorrespondingFlow; break;
    case NetworkMatchReason::ProtocolMismatch: ++networkMetrics_.networkProtocolMismatch; break;
    case NetworkMatchReason::PortMismatch: ++networkMetrics_.networkPortMismatch; break;
    case NetworkMatchReason::LocalPortMismatch: ++networkMetrics_.networkPortMismatch; ++networkMetrics_.networkLocalPortMismatch; break;
    case NetworkMatchReason::RemotePortMismatch: ++networkMetrics_.networkPortMismatch; ++networkMetrics_.networkRemotePortMismatch; break;
    case NetworkMatchReason::AddressMismatch: ++networkMetrics_.networkAddressMismatch; break;
    case NetworkMatchReason::LocalAddressMismatch: ++networkMetrics_.networkAddressMismatch; ++networkMetrics_.networkLocalAddressMismatch; break;
    case NetworkMatchReason::RemoteAddressMismatch: ++networkMetrics_.networkAddressMismatch; ++networkMetrics_.networkRemoteAddressMismatch; break;
    case NetworkMatchReason::DirectionMismatch: ++networkMetrics_.networkDirectionMismatch; break;
    case NetworkMatchReason::BeforeFlowWindow: ++networkMetrics_.networkBeforeFlowWindow; ++networkMetrics_.networkOutsideAttributionWindow; break;
    case NetworkMatchReason::ExpiredFlow: ++networkMetrics_.networkExpiredFlow; break;
    case NetworkMatchReason::AfterFlowClosed: ++networkMetrics_.networkAfterFlowClosed; ++networkMetrics_.networkOutsideAttributionWindow; break;
    case NetworkMatchReason::FamilyMismatch: ++networkMetrics_.networkFamilyMismatch; break;
    case NetworkMatchReason::UnverifiedIdentity: ++networkMetrics_.networkUnverifiedIdentity; break;
    default: break;
  }
}

void CaptureController::retryPendingNetworkPackets(bool flush) {
  using namespace std::chrono_literals;
  const auto now=std::chrono::steady_clock::now();
  constexpr auto correlationWindow=500ms;
  for(auto it=pendingNetworkCorrelations_.begin();it!=pendingNetworkCorrelations_.end();) {
    const bool expired=now-it->queuedAt>=correlationWindow;
    if(!flush&&!expired){++it;continue;}
    const auto result=tracker_.diagnoseNetworkPacket(it->metadata,it->outbound,it->timestampNs);
    if(result.match==NetworkFlowMatch::MatchedSteam ||
       result.match==NetworkFlowMatch::Ambiguous || flush || expired) {
      accountNetworkMatch(result);
      const auto latency=static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(now-it->queuedAt).count());
      { std::lock_guard lock(stateMutex_);
        ++networkMetrics_.networkCorrelationsFinalized;
        networkMetrics_.correlationLatencyTotalNs+=latency;
        networkMetrics_.correlationLatencyMaxNs=std::max(networkMetrics_.correlationLatencyMaxNs,latency);
      }
      it=pendingNetworkCorrelations_.erase(it);
    } else ++it;
  }
}

void CaptureController::enqueue(CaptureEvent event) {
  if (shuttingDown_) return;
  const bool flowEvent=event.layer!=CaptureLayer::Network;
  if(event.layer==CaptureLayer::Network) {
    {
      std::lock_guard lock(stateMutex_);
      ++networkMetrics_.networkPacketsObserved;
      networkMetrics_.networkBytesObserved+=event.packetBytes.size();
      networkMetrics_.captureLatencyTotalNs+=event.captureLatencyNs;
      networkMetrics_.captureLatencyMaxNs=std::max(networkMetrics_.captureLatencyMaxNs,event.captureLatencyNs);
    }
    if(networkProfile_==NetworkCaptureProfile::VerifiedSteamEndpointCandidates) {
      const auto peek=peekTransportTuple(event.packetBytes);
      const auto scopes=verifiedSteamScopes_.load();
      if(peek.valid&&(!scopes||scopes->empty())) {
        std::lock_guard lock(stateMutex_);
        ++networkMetrics_.networkPacketsFilteredBeforeParser;
        ++networkMetrics_.networkFilteredNoVerifiedFlow;
        return;
      }
      bool inScope=false;
      if(peek.valid&&scopes) for(const auto& scope:*scopes) {
        const bool ipv6=peek.ipv6;
        const bool before=scope.firstTimestampNs&&event.timestampNs<scope.firstTimestampNs&&
                          scope.firstTimestampNs-event.timestampNs>500000000ull;
        const bool expired=scope.firstTimestampNs&&event.timestampNs>scope.firstTimestampNs&&
                           event.timestampNs-scope.firstTimestampNs>3600000000000ull;
        const bool closed=scope.closedTimestampNs&&event.timestampNs>scope.closedTimestampNs&&
                          event.timestampNs-scope.closedTimestampNs>500000000ull;
        if(!before&&!expired&&!closed&&scope.ipv6==ipv6&&scope.protocol==peek.protocol&&
           ((scope.localPort==peek.sourcePort&&scope.remotePort==peek.destinationPort)||
            (scope.localPort==peek.destinationPort&&scope.remotePort==peek.sourcePort))) {inScope=true;break;}
      }
      if(peek.valid&&!inScope) {
        std::lock_guard lock(stateMutex_);
        ++networkMetrics_.networkPacketsFilteredBeforeParser;
        ++networkMetrics_.networkFilteredOutsideEndpointScope;
        return;
      }
    }
  }
  if(flowEvent)++eventOutstanding_;
  const bool queued=event.layer==CaptureLayer::Network
      ? networkEvents_.push(std::move(event)) : events_.push(std::move(event));
  if (!queued) {
    if(flowEvent){--eventOutstanding_;drainChanged_.notify_all();}
    ++droppedEvents_;
    if(flowEvent)++droppedFlowSocketEvents_;else ++droppedNetworkEvents_;
  }
}

void CaptureController::refreshSteamCandidateScopes() {
  verifiedSteamScopes_.store(std::make_shared<const std::vector<VerifiedFlowPortScope>>(
      tracker_.verifiedSteamPortScopes()),std::memory_order_release);
}

void CaptureController::publish(bool force) {
  SnapshotCallback callback;
  {
    std::lock_guard lock(stateMutex_);
    const auto now = std::chrono::steady_clock::now();
    if (!force && lastPublished_.time_since_epoch().count() != 0 &&
        now - lastPublished_ < std::chrono::milliseconds(250)) return;
    lastPublished_ = now;
    callback = snapshotCallback_;
  }
  if (callback) callback(snapshot());
}

void CaptureController::setState(CaptureState state, std::string status, std::uint32_t error) {
  std::lock_guard lock(stateMutex_);
  snapshot_.state = state;
  snapshot_.status = std::move(status);
  snapshot_.lastError = error;
}

} // namespace steamdpi
