#pragma once

#include "CaptureTypes.h"
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace steamdpi {

class ProcessIdentityResolver {
public:
  ProcessIdentityResolver();
  explicit ProcessIdentityResolver(std::vector<std::wstring> steamRoots);
  std::optional<ProcessIdentity> resolve(std::uint32_t processId);

  static bool isVerifiedSteamImagePath(const std::wstring& imagePath,
                                       const std::vector<std::wstring>& steamRoots,
                                       bool trustedValveSignature);

private:
  struct CacheEntry {
    std::uint64_t creationTime = 0;
    std::optional<ProcessIdentity> identity;
  };

  std::vector<std::wstring> roots_;
  std::mutex mutex_;
  std::unordered_map<std::uint32_t, CacheEntry> cache_;
};

} // namespace steamdpi
