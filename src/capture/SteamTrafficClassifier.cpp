#include "SteamTrafficClassifier.h"
#include <algorithm>
#include <cwctype>
#include <filesystem>

namespace steamdpi {
namespace {
bool samePath(const std::wstring& first, const std::wstring& second) {
  return first.size() == second.size() && std::equal(first.begin(), first.end(), second.begin(),
      [](wchar_t a, wchar_t b) { return std::towlower(a) == std::towlower(b); });
}

bool correlatedEvents(const CaptureEvent& flow, const CaptureEvent& socket) {
  return flow.layer == CaptureLayer::Flow && flow.kind == CaptureEventKind::Established &&
         socket.layer == CaptureLayer::Socket &&
         (socket.kind == CaptureEventKind::Connect || socket.kind == CaptureEventKind::Accept) &&
         flow.endpointId != 0 && flow.endpointId == socket.endpointId &&
         flow.parentEndpointId == socket.parentEndpointId &&
         flow.processId == socket.processId && flow.tuple == socket.tuple &&
         (flow.tuple.protocol == 6 || flow.tuple.protocol == 17);
}
}

SteamExecutableClass SteamTrafficClassifier::classifyFlow(
    const CaptureEvent& flow, const CaptureEvent& socket,
    const std::optional<ProcessIdentity>& flowIdentity,
    const std::optional<ProcessIdentity>& socketIdentity) const {
  if (!correlatedEvents(flow, socket) || flow.processId == 0 || !flowIdentity || !socketIdentity)
    return SteamExecutableClass::UnknownOrAmbiguous;
  if (flow.processId != flowIdentity->processId || flow.processId != socketIdentity->processId ||
      !flowIdentity->signatureVerified || !socketIdentity->signatureVerified ||
      flowIdentity->processCreationTime != socketIdentity->processCreationTime ||
      !samePath(flowIdentity->imagePath, socketIdentity->imagePath))
    return SteamExecutableClass::UnknownOrAmbiguous;

  auto image = std::filesystem::path(flowIdentity->imagePath).filename().wstring();
  std::transform(image.begin(), image.end(), image.begin(),
                 [](wchar_t c) { return std::towlower(c); });
  if (image == L"steam.exe") return SteamExecutableClass::SteamClient;
  if (image == L"steamwebhelper.exe") return SteamExecutableClass::SteamWebHelper;
  return SteamExecutableClass::UnknownOrAmbiguous;
}

bool SteamTrafficClassifier::isCorrelatedSteamFlow(
    const CaptureEvent& flow, const CaptureEvent& socket,
    const ProcessIdentity& flowIdentity, const ProcessIdentity& socketIdentity) const {
  const auto result = classifyFlow(flow, socket, flowIdentity, socketIdentity);
  return result == SteamExecutableClass::SteamClient ||
         result == SteamExecutableClass::SteamWebHelper;
}

} // namespace steamdpi
