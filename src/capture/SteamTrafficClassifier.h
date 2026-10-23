#pragma once

#include "CaptureTypes.h"
#include <optional>

namespace steamdpi {

enum class SteamExecutableClass { SteamClient, SteamWebHelper, UnknownOrAmbiguous };

class SteamTrafficClassifier {
public:
  SteamExecutableClass classifyFlow(const CaptureEvent& flow,
                                    const CaptureEvent& socket,
                                    const std::optional<ProcessIdentity>& flowIdentity,
                                    const std::optional<ProcessIdentity>& socketIdentity) const;
  bool isCorrelatedSteamFlow(const CaptureEvent& flow, const CaptureEvent& socket,
                             const ProcessIdentity& flowIdentity,
                             const ProcessIdentity& socketIdentity) const;
};

} // namespace steamdpi
