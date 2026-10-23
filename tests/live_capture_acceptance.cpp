#include "capture/CaptureController.h"
#include <windows.h>
#include <psapi.h>
#include <tlhelp32.h>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <cwchar>
#include <iostream>
#include <thread>

namespace {
struct ResourceSample {
  SIZE_T workingSet = 0;
  DWORD handles = 0;
  ULONGLONG cpuTime100ns = 0;
  double cpuPercent = 0;
};

ResourceSample sampleResources() {
  ResourceSample sample;
  PROCESS_MEMORY_COUNTERS counters{};
  counters.cb = sizeof(counters);
  GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters));
  GetProcessHandleCount(GetCurrentProcess(), &sample.handles);
  sample.workingSet = counters.WorkingSetSize;
  FILETIME create{},exit{},kernel{},user{};
  if(GetProcessTimes(GetCurrentProcess(),&create,&exit,&kernel,&user)){
    ULARGE_INTEGER k{},u{};k.LowPart=kernel.dwLowDateTime;k.HighPart=kernel.dwHighDateTime;
    u.LowPart=user.dwLowDateTime;u.HighPart=user.dwHighDateTime;sample.cpuTime100ns=k.QuadPart+u.QuadPart;
  }
  return sample;
}

struct ThreadCounts { DWORD total = 0; DWORD steamDpiCaptureWorkers = 0; };

ThreadCounts processThreadCounts() {
  ThreadCounts counts;
  const DWORD pid = GetCurrentProcessId();
  HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
  if (snapshot == INVALID_HANDLE_VALUE) return counts;
  THREADENTRY32 entry{};
  entry.dwSize = sizeof(entry);
  if (Thread32First(snapshot, &entry)) {
    do {
      if (entry.th32OwnerProcessID != pid) continue;
      ++counts.total;
      HANDLE thread = OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ThreadID);
      if (!thread) continue;
      PWSTR description = nullptr;
      if (SUCCEEDED(GetThreadDescription(thread, &description)) && description) {
        if (wcsncmp(description, L"SteamDPI ", 9) == 0)
          ++counts.steamDpiCaptureWorkers;
        LocalFree(description);
      }
      CloseHandle(thread);
    } while (Thread32Next(snapshot, &entry));
  }
  CloseHandle(snapshot);
  return counts;
}
}

int main(int argc, char** argv) {
  using namespace std::chrono_literals;
  if (argc == 0) return 1;
  const auto observeFor = argc > 1 ? std::chrono::seconds(std::stoi(argv[1])) : 60s;
  const std::filesystem::path stopFile = argc > 2 ? argv[2] : "";
  std::ofstream log(std::filesystem::path(argv[0]).parent_path() /
                    "live_capture_acceptance.log", std::ios::trunc);
  const auto write = [&log](const std::string& line) {
    std::cout << line << '\n' << std::flush;
    if (log) log << line << '\n' << std::flush;
  };
  if (!steamdpi::CaptureController::isAdministrator()) {
    write("FAIL: administrator token required");
    return 2;
  }

  const auto initial = sampleResources();
  ResourceSample peak = initial;
  unsigned samples = 1;
  ThreadCounts threadsAfterShutdown{};
  {
    steamdpi::CaptureController capture;
    for (int cycle = 1; cycle <= 10; ++cycle) {
      std::string error;
      if (!capture.start(error)) {
        write("FAIL: cycle " + std::to_string(cycle) + " start: " + error);
        return 3;
      }
      std::this_thread::sleep_for(250ms);
      capture.stop();
      if (capture.snapshot().state != steamdpi::CaptureState::Stopped) {
        write("FAIL: cycle " + std::to_string(cycle) + " did not stop cleanly");
        return 4;
      }
      DWORD handlesAfterStop = 0;
      GetProcessHandleCount(GetCurrentProcess(), &handlesAfterStop);
      write("cycle=" + std::to_string(cycle) + " stopped handles=" +
            std::to_string(handlesAfterStop));
    }

    std::string error;
    if (!capture.start(error)) {
      write("FAIL: observation start after cycles: " + error);
      return 5;
    }
    write("LIVE_CAPTURE_STARTED");
    const auto deadline = std::chrono::steady_clock::now() + observeFor;
    auto nextSample = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() < deadline) {
      if (!stopFile.empty() && std::filesystem::exists(stopFile)) {
        const auto metrics = capture.snapshot().metrics;
        capture.stop();
        write("SAFETY_STOP_REQUESTED steam_flows=" + std::to_string(metrics.steamFlowsObserved) +
              " steam_exe=" + std::to_string(metrics.steamExeFlows) +
              " steamwebhelper=" + std::to_string(metrics.steamWebHelperFlows) +
              " unknown_or_ambiguous=" + std::to_string(metrics.unknownOrAmbiguousFlows) +
              " network_packets=" + std::to_string(metrics.networkPacketsObserved) +
              " network_matched=" + std::to_string(metrics.networkPacketsMatchedSteam) +
              " network_unmatched=" + std::to_string(metrics.networkPacketsUnmatched) +
              " network_ambiguous=" + std::to_string(metrics.networkPacketsAmbiguous));
        return 8;
      }
      if (std::chrono::steady_clock::now() >= nextSample) {
        const auto current = sampleResources();
        peak.workingSet = (std::max)(peak.workingSet, current.workingSet);
        peak.handles = (std::max)(peak.handles, current.handles);
        const auto interval=std::chrono::duration<double>(std::chrono::steady_clock::now()-nextSample+5s).count();
        if(interval>0&&current.cpuTime100ns>=peak.cpuTime100ns){
          const auto previousCpu=peak.cpuTime100ns;const auto delta=current.cpuTime100ns-previousCpu;
          const auto logical=static_cast<double>((std::max<DWORD>)(1,GetActiveProcessorCount(ALL_PROCESSOR_GROUPS)));
          peak.cpuPercent=100.0*(static_cast<double>(delta)/10000000.0)/(interval*logical);
        }
        peak.cpuPercent=(std::max)(peak.cpuPercent,current.cpuPercent);
        peak.cpuTime100ns=current.cpuTime100ns;
        ++samples;
        nextSample = std::chrono::steady_clock::now() + 5s;
      }
      std::this_thread::sleep_for(250ms);
    }
    capture.stop();
    if (capture.snapshot().state != steamdpi::CaptureState::Stopped) {
      write("FAIL: final Stop did not complete");
      return 6;
    }
    const auto metrics = capture.snapshot().metrics;
    write("steam_flows=" + std::to_string(metrics.steamFlowsObserved) +
          " steam_exe=" + std::to_string(metrics.steamExeFlows) +
          " steamwebhelper=" + std::to_string(metrics.steamWebHelperFlows) +
          " unknown_or_ambiguous=" + std::to_string(metrics.unknownOrAmbiguousFlows) +
          " flow_events=" + std::to_string(metrics.flowEventsObserved) +
          " socket_events=" + std::to_string(metrics.socketEventsObserved) +
          " flow_socket_queue_drops=" + std::to_string(metrics.flowSocketEventQueueDrops) +
          " tcp=" + std::to_string(metrics.tcpFlows) +
          " udp=" + std::to_string(metrics.udpFlows) +
          " ipv4=" + std::to_string(metrics.ipv4Flows) +
          " ipv6=" + std::to_string(metrics.ipv6Flows) +
          " errors=" + std::to_string(metrics.observationErrors) +
          " dropped_monitor_events=" + std::to_string(metrics.droppedMonitorEvents) +
          " network_packets=" + std::to_string(metrics.networkPacketsObserved) +
          " filtered_before_parser=" + std::to_string(metrics.networkPacketsFilteredBeforeParser) +
          " discarded_inactive=" + std::to_string(metrics.networkPacketsDiscardedInactiveState) +
          " filtered_no_verified_flow=" + std::to_string(metrics.networkFilteredNoVerifiedFlow) +
          " filtered_outside_endpoint_scope=" + std::to_string(metrics.networkFilteredOutsideEndpointScope) +
          " network_parsed=" + std::to_string(metrics.networkPacketsParsed) +
          " network_ipv4=" + std::to_string(metrics.networkIpv4Packets) +
          " network_ipv6=" + std::to_string(metrics.networkIpv6Packets) +
          " network_rejected=" + std::to_string(metrics.networkPacketsRejected) +
          " rejected_checksum=" + std::to_string(metrics.networkRejectedChecksum) +
          " rejected_truncated=" + std::to_string(metrics.networkRejectedTruncated) +
          " rejected_unsupported=" + std::to_string(metrics.networkRejectedUnsupported) +
          " rejected_length=" + std::to_string(metrics.networkRejectedLength) +
          " rejected_other=" + std::to_string(metrics.networkRejectedOther) +
          " network_checksum_unverified=" + std::to_string(metrics.networkPacketsChecksumUnverified) +
          " network_matched_steam=" + std::to_string(metrics.networkPacketsMatchedSteam) +
          " network_unmatched=" + std::to_string(metrics.networkPacketsUnmatched) +
          " network_ambiguous=" + std::to_string(metrics.networkPacketsAmbiguous) +
          " unmatched_no_flow=" + std::to_string(metrics.networkNoCorrespondingFlow) +
          " unmatched_protocol=" + std::to_string(metrics.networkProtocolMismatch) +
          " unmatched_port=" + std::to_string(metrics.networkPortMismatch) +
          " unmatched_local_port=" + std::to_string(metrics.networkLocalPortMismatch) +
          " unmatched_remote_port=" + std::to_string(metrics.networkRemotePortMismatch) +
          " unmatched_address=" + std::to_string(metrics.networkAddressMismatch) +
          " unmatched_local_address=" + std::to_string(metrics.networkLocalAddressMismatch) +
          " unmatched_remote_address=" + std::to_string(metrics.networkRemoteAddressMismatch) +
          " unmatched_direction=" + std::to_string(metrics.networkDirectionMismatch) +
          " unmatched_pre_flow=" + std::to_string(metrics.networkBeforeFlowWindow) +
          " unmatched_outside_window=" + std::to_string(metrics.networkOutsideAttributionWindow) +
          " unmatched_expired=" + std::to_string(metrics.networkExpiredFlow) +
          " unmatched_unverified_identity=" + std::to_string(metrics.networkUnverifiedIdentity) +
          " unmatched_family=" + std::to_string(metrics.networkFamilyMismatch) +
          " correlation_queue_drops=" + std::to_string(metrics.networkCorrelationQueueDrops) +
          " correlation_queue_high_water=" + std::to_string(metrics.networkCorrelationQueueHighWater) +
          " correlation_candidates_queued=" + std::to_string(metrics.networkCorrelationCandidatesQueued) +
          " correlations_finalized=" + std::to_string(metrics.networkCorrelationsFinalized) +
          " network_queue_high_water=" + std::to_string(metrics.networkQueueHighWater) +
          " network_events_enqueued=" + std::to_string(metrics.networkEventsEnqueued) +
          " network_events_dequeued=" + std::to_string(metrics.networkEventsDequeued) +
          " network_capture_queue_drops=" + std::to_string(metrics.networkCaptureQueueDrops) +
          " flow_socket_queue_high_water=" + std::to_string(metrics.flowSocketQueueHighWater) +
          " flow_socket_events_enqueued=" + std::to_string(metrics.flowSocketEventsEnqueued) +
          " flow_socket_events_dequeued=" + std::to_string(metrics.flowSocketEventsDequeued) +
          " flow_protocol_candidates=" + std::to_string(metrics.networkFlowProtocolCandidates) +
          " flow_port_candidates=" + std::to_string(metrics.networkFlowPortCandidates) +
          " flow_address_candidates=" + std::to_string(metrics.networkFlowAddressCandidates) +
          " flow_family_candidates=" + std::to_string(metrics.networkFlowFamilyCandidates) +
          " steam_flow_protocol_candidates=" + std::to_string(metrics.steamFlowProtocolCandidates) +
          " steam_flow_port_candidates=" + std::to_string(metrics.steamFlowPortCandidates) +
          " steam_flow_address_candidates=" + std::to_string(metrics.steamFlowAddressCandidates) +
          " steam_local_address_candidates=" + std::to_string(metrics.steamFlowLocalAddressCandidates) +
          " steam_remote_address_candidates=" + std::to_string(metrics.steamFlowRemoteAddressCandidates) +
          " steam_flow_word_swapped_address_candidates=" + std::to_string(metrics.steamFlowWordSwappedAddressCandidates) +
          " flow_direction_mismatches=" + std::to_string(metrics.networkFlowDirectionMismatches) +
          " network_bytes=" + std::to_string(metrics.networkBytesObserved) +
          " network_bytes_per_second=" + std::to_string(metrics.networkBytesObserved /
              static_cast<std::uint64_t>((std::max<std::int64_t>)(1,observeFor.count()))) +
          " capture_latency_max_ns=" + std::to_string(metrics.captureLatencyMaxNs) +
          " parse_latency_max_ns=" + std::to_string(metrics.parseLatencyMaxNs));
    write("rates_per_second network_received=" + std::to_string(metrics.networkPacketsObserved /
              static_cast<std::uint64_t>((std::max<std::int64_t>)(1,observeFor.count()))) +
          " network_queued=" + std::to_string(metrics.networkEventsEnqueued /
              static_cast<std::uint64_t>((std::max<std::int64_t>)(1,observeFor.count()))) +
          " network_dequeued=" + std::to_string(metrics.networkEventsDequeued /
              static_cast<std::uint64_t>((std::max<std::int64_t>)(1,observeFor.count()))) +
          " network_parsed=" + std::to_string(metrics.networkPacketsParsed /
              static_cast<std::uint64_t>((std::max<std::int64_t>)(1,observeFor.count()))) +
          " correlations_finalized=" + std::to_string(metrics.networkCorrelationsFinalized /
              static_cast<std::uint64_t>((std::max<std::int64_t>)(1,observeFor.count()))) +
          " parse_latency_avg_ns=" + std::to_string(metrics.networkPacketsParsed?
              metrics.parseLatencyTotalNs/metrics.networkPacketsParsed:0) +
          " correlation_latency_avg_ns=" + std::to_string(metrics.networkCorrelationsFinalized?
              metrics.correlationLatencyTotalNs/metrics.networkCorrelationsFinalized:0));
    {
      steamdpi::CaptureController closing;
      if (!closing.start(error)) {
        write("FAIL: shutdown fixture start: " + error);
        return 7;
      }
    }
    write("PASS: 10 Start/Stop cycles; active-controller destruction; clean handle shutdown");
  }
  const auto final = sampleResources();
  write("resources_samples=" + std::to_string(samples) +
        " working_set_initial=" + std::to_string(initial.workingSet) +
        " working_set_peak=" + std::to_string(peak.workingSet) +
        " working_set_final=" + std::to_string(final.workingSet) +
        " cpu_peak_percent=" + std::to_string(peak.cpuPercent) +
        " handles_initial=" + std::to_string(initial.handles) +
        " handles_peak=" + std::to_string(peak.handles) +
        " handles_final=" + std::to_string(final.handles));
  threadsAfterShutdown = processThreadCounts();
  write("process_threads_after_shutdown=" + std::to_string(threadsAfterShutdown.total) +
        " steamdpi_capture_workers_after_shutdown=" +
        std::to_string(threadsAfterShutdown.steamDpiCaptureWorkers));
  if (threadsAfterShutdown.steamDpiCaptureWorkers != 0) {
    write("FAIL: a named SteamDPI capture worker remained after controller destruction");
    return 9;
  }
  return 0;
}
