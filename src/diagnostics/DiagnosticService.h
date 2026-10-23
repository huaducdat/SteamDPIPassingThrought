#pragma once
#include <atomic>
#include <functional>
#include <string>
#include <vector>
namespace steamdpi {
enum class ProbeStatus { Success, Failure, Timeout, Cancelled };
inline const char* probeStatusName(ProbeStatus status) {
  switch (status) {
    case ProbeStatus::Success: return "SUCCESS";
    case ProbeStatus::Timeout: return "TIMEOUT";
    case ProbeStatus::Cancelled: return "CANCELLED";
    default: return "FAILURE";
  }
}
struct ProbeResult { std::string host, stage, detail; ProbeStatus status = ProbeStatus::Failure; long long latencyMs = 0; long long checkedAtUnixMs = 0; };
struct DiagnosticReport { std::vector<ProbeResult> results; };
class DiagnosticService {
 public:
  using Progress = std::function<void(const ProbeResult&)>;
  DiagnosticReport run(const std::vector<std::string>& hosts, int timeoutMs, int concurrency, const std::atomic_bool& cancel, Progress progress = {}) const;
};
}
