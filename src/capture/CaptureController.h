#pragma once

#include "BoundedEventQueue.h"
#include "CaptureAdapter.h"
#include "CaptureTypes.h"
#include "FlowTracker.h"
#include "packet/PacketParser.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace steamdpi {

class CaptureController {
public:
  using IdentityLookup = std::function<std::optional<ProcessIdentity>(std::uint32_t)>;
  using SnapshotCallback = std::function<void(const CaptureSnapshot&)>;

  explicit CaptureController(std::unique_ptr<CaptureAdapter> adapter = {},
                             IdentityLookup identityLookup = {},
                             bool requireAdministrator = true,
                             NetworkCaptureProfile profile = NetworkCaptureProfile::VerifiedSteamEndpointCandidates);
  ~CaptureController();
  CaptureController(const CaptureController&) = delete;
  CaptureController& operator=(const CaptureController&) = delete;

  bool start(std::string& error);
  void stop();
  CaptureSnapshot snapshot() const;
  void setSnapshotCallback(SnapshotCallback callback);
  static bool isAdministrator();

private:
  void processEvents();
  void processNetworkEvents();
  void processNetworkPacket(CaptureEvent event);
  void retryPendingNetworkPackets(bool flush = false);
  void accountNetworkMatch(const NetworkMatchResult& result);
  void refreshSteamCandidateScopes();
  void enqueue(CaptureEvent event);
  void publish(bool force = false);
  void setState(CaptureState state, std::string status, std::uint32_t error = 0);

  std::unique_ptr<CaptureAdapter> adapter_;
  IdentityLookup identityLookup_;
  const bool requireAdministrator_;
  const NetworkCaptureProfile networkProfile_;
  std::atomic<std::shared_ptr<const std::vector<VerifiedFlowPortScope>>> verifiedSteamScopes_;
  BoundedEventQueue<CaptureEvent> events_{512};
  BoundedEventQueue<CaptureEvent> networkEvents_{128};
  FlowTracker tracker_;
  std::thread processingThread_;
  std::thread networkProcessingThread_;
  mutable std::mutex stateMutex_;
  std::mutex lifecycleMutex_;
  CaptureSnapshot snapshot_;
  CaptureMetrics networkMetrics_;
  struct PendingNetworkCorrelation {
    packet::PacketMetadata metadata;
    bool outbound = false;
    std::uint64_t timestampNs = 0;
    std::chrono::steady_clock::time_point queuedAt;
  };
  std::vector<PendingNetworkCorrelation> pendingNetworkCorrelations_;
  static constexpr std::size_t kPendingCorrelationCapacity=4096;
  std::atomic<std::uint64_t> eventOutstanding_{0};
  std::atomic_bool flushCorrelations_{false};
  std::mutex drainMutex_;
  std::condition_variable drainChanged_;
  SnapshotCallback snapshotCallback_;
  std::chrono::steady_clock::time_point lastPublished_{};
  std::atomic<std::uint64_t> droppedEvents_{0};
  std::atomic<std::uint64_t> droppedNetworkEvents_{0};
  std::atomic<std::uint64_t> droppedFlowSocketEvents_{0};
  std::atomic_bool shuttingDown_{false};
};

} // namespace steamdpi
