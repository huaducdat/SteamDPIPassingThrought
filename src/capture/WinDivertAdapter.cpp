#include "WinDivertAdapter.h"
#include "AddressNormalization.h"
#include <algorithm>
#include <array>
#include <string>

namespace steamdpi {
namespace {
CaptureEventKind eventKind(CaptureLayer layer, UINT event) {
  if (layer == CaptureLayer::Flow) {
    if (event == WINDIVERT_EVENT_FLOW_ESTABLISHED) return CaptureEventKind::Established;
    if (event == WINDIVERT_EVENT_FLOW_DELETED) return CaptureEventKind::Deleted;
  } else {
    if (event == WINDIVERT_EVENT_SOCKET_CONNECT) return CaptureEventKind::Connect;
    if (event == WINDIVERT_EVENT_SOCKET_ACCEPT) return CaptureEventKind::Accept;
    if (event == WINDIVERT_EVENT_SOCKET_CLOSE) return CaptureEventKind::Close;
    if (event == WINDIVERT_EVENT_SOCKET_BIND || event == WINDIVERT_EVENT_SOCKET_LISTEN)
      return CaptureEventKind::Other;
  }
  return CaptureEventKind::Error;
}

void copyAddressBytes(std::array<UINT8,16>& destination,const UINT32* address) {
  // WinDivert's FLOW/SOCKET arrays are four host-order UINT32 words (the
  // official flowtrack sample passes them directly to FormatIPv6Address).
  // Convert each numeric word into canonical network-order bytes.
  std::array<std::uint32_t,4> words{};
  std::copy_n(address,words.size(),words.begin());
  destination=canonicalWinDivertAddress(words);
}
void copyAddress(FlowTuple& tuple, const UINT32* local, const UINT32* remote,
                 UINT16 localPort, UINT16 remotePort, UINT8 protocol, bool ipv6) {
  copyAddressBytes(tuple.localAddress,local);
  copyAddressBytes(tuple.remoteAddress,remote);
  tuple.localPort = localPort;
  tuple.remotePort = remotePort;
  tuple.protocol = protocol;
  tuple.ipv6 = ipv6;
}

std::string explainDriverError(DWORD code) {
  if (code == ERROR_ACCESS_DENIED || code == ERROR_ELEVATION_REQUIRED)
    return "Administrator permission is required";
  if (code == ERROR_FILE_NOT_FOUND || code == ERROR_SERVICE_DOES_NOT_EXIST)
    return "WinDivert driver is unavailable beside the application";
  if (code == ERROR_INVALID_IMAGE_HASH)
    return "Windows rejected the WinDivert driver signature";
  if (code == ERROR_DRIVER_BLOCKED)
    return "Windows or security software blocked the WinDivert driver";
  if (code == ERROR_DRIVER_FAILED_PRIOR_UNLOAD)
    return "An incompatible WinDivert driver version is already loaded";
  return "WinDivert could not open an observation handle";
}

std::uint64_t qpcNanoseconds(LARGE_INTEGER value) {
  static const std::int64_t frequency=[] { LARGE_INTEGER f{};return QueryPerformanceFrequency(&f)?f.QuadPart:0; }();
  if (frequency <= 0) return 0;
  return static_cast<std::uint64_t>((static_cast<long double>(value.QuadPart) * 1000000000.0L) / frequency);
}
}

WinDivertAdapter::~WinDivertAdapter() { stop(); }

bool WinDivertAdapter::start(EventCallback onEvent, ErrorCallback onError,
                             std::string& error) {
  std::unique_lock lock(mutex_);
  if (flowHandle_ != INVALID_HANDLE_VALUE || socketHandle_ != INVALID_HANDLE_VALUE || networkHandle_ != INVALID_HANDLE_VALUE) {
    error = "Capture adapter is already open";
    return false;
  }
  onEvent_ = std::move(onEvent);
  onError_ = std::move(onError);
  stopping_ = false;
  lastStopError_ = ERROR_SUCCESS;
  module_ = LoadLibraryExW(L"WinDivert.dll", nullptr,
      LOAD_LIBRARY_SEARCH_APPLICATION_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
  if (!module_) {
    const DWORD code = GetLastError();
    error = "WinDivert 2.2.1 x64 runtime is unavailable (Win32 " + std::to_string(code) + ")";
    SetLastError(code);
    onEvent_ = {};
    onError_ = {};
    return false;
  }
  open_ = reinterpret_cast<decltype(open_)>(GetProcAddress(module_, "WinDivertOpen"));
  recv_ = reinterpret_cast<decltype(recv_)>(GetProcAddress(module_, "WinDivertRecv"));
  shutdown_ = reinterpret_cast<decltype(shutdown_)>(GetProcAddress(module_, "WinDivertShutdown"));
  close_ = reinterpret_cast<decltype(close_)>(GetProcAddress(module_, "WinDivertClose"));
  if (!open_ || !recv_ || !shutdown_ || !close_) {
    const DWORD code = ERROR_PROC_NOT_FOUND;
    FreeLibrary(module_);
    module_ = nullptr;
    error = "WinDivert runtime API is incompatible (Win32 " + std::to_string(code) + ")";
    SetLastError(code);
    onEvent_ = {};
    onError_ = {};
    return false;
  }
  constexpr UINT64 readOnlySniff = WINDIVERT_FLAG_SNIFF | WINDIVERT_FLAG_RECV_ONLY;
  flowHandle_ = open_("tcp or udp", WINDIVERT_LAYER_FLOW, 0, readOnlySniff);
  if (flowHandle_ == INVALID_HANDLE_VALUE) {
    const DWORD code = GetLastError();
    error = explainDriverError(code) + " (FLOW, Win32 " + std::to_string(code) + ")";
    FreeLibrary(module_);
    module_ = nullptr;
    SetLastError(code);
    onEvent_ = {};
    onError_ = {};
    return false;
  }
  socketHandle_ = open_("tcp or udp", WINDIVERT_LAYER_SOCKET, 0, readOnlySniff);
  if (socketHandle_ == INVALID_HANDLE_VALUE) {
    const DWORD code = GetLastError();
    if (!close_(flowHandle_)) lastStopError_ = GetLastError();
    flowHandle_ = INVALID_HANDLE_VALUE;
    error = explainDriverError(code) + " (SOCKET, Win32 " + std::to_string(code) + ")";
    FreeLibrary(module_);
    module_ = nullptr;
    SetLastError(code);
    onEvent_ = {};
    onError_ = {};
    return false;
  }
  networkHandle_ = open_("tcp or udp", WINDIVERT_LAYER_NETWORK, 0, readOnlySniff);
  if (networkHandle_ == INVALID_HANDLE_VALUE) {
    const DWORD code = GetLastError();
    if (!close_(flowHandle_)) lastStopError_ = GetLastError();
    if (!close_(socketHandle_) && lastStopError_ == ERROR_SUCCESS) lastStopError_ = GetLastError();
    flowHandle_ = socketHandle_ = INVALID_HANDLE_VALUE;
    error = explainDriverError(code) + " (NETWORK SNIFF, Win32 " + std::to_string(code) + ")";
    FreeLibrary(module_); module_ = nullptr; SetLastError(code); onEvent_ = {}; onError_ = {};
    return false;
  }
  try {
    flowThread_ = std::thread([this] {
      SetThreadDescription(GetCurrentThread(), L"SteamDPI WinDivert FLOW Worker");
      receive(flowHandle_, CaptureLayer::Flow);
    });
    socketThread_ = std::thread([this] {
      SetThreadDescription(GetCurrentThread(), L"SteamDPI WinDivert SOCKET Worker");
      receive(socketHandle_, CaptureLayer::Socket);
    });
    networkThread_ = std::thread([this] {
      SetThreadDescription(GetCurrentThread(), L"SteamDPI WinDivert NETWORK SNIFF Worker");
      receive(networkHandle_, CaptureLayer::Network);
    });
  } catch (const std::system_error&) {
    stopping_ = true;
    shutdown_(flowHandle_, WINDIVERT_SHUTDOWN_RECV);
    shutdown_(socketHandle_, WINDIVERT_SHUTDOWN_RECV);
    shutdown_(networkHandle_, WINDIVERT_SHUTDOWN_RECV);
    auto flowThread = std::move(flowThread_);
    auto socketThread = std::move(socketThread_);
    auto networkThread = std::move(networkThread_);
    lock.unlock();
    if (flowThread.joinable()) flowThread.join();
    if (socketThread.joinable()) socketThread.join();
    if (networkThread.joinable()) networkThread.join();
    lock.lock();
    DWORD closeError = ERROR_SUCCESS;
    if (!close_(flowHandle_)) closeError = GetLastError();
    if (!close_(socketHandle_) && closeError == ERROR_SUCCESS) closeError = GetLastError();
    if (!close_(networkHandle_) && closeError == ERROR_SUCCESS) closeError = GetLastError();
    lastStopError_ = closeError;
    flowHandle_ = socketHandle_ = networkHandle_ = INVALID_HANDLE_VALUE;
    FreeLibrary(module_);
    module_ = nullptr;
    open_ = nullptr; recv_ = nullptr; shutdown_ = nullptr; close_ = nullptr;
    onEvent_ = {};
    onError_ = {};
    error = "Unable to start WinDivert observation workers";
    SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return false;
  }
  return true;
}

void WinDivertAdapter::stop() {
  std::thread flowThread;
  std::thread socketThread;
  std::thread networkThread;
  {
    std::lock_guard lock(mutex_);
    stopping_ = true;
    if (flowHandle_ != INVALID_HANDLE_VALUE && shutdown_) shutdown_(flowHandle_, WINDIVERT_SHUTDOWN_RECV);
    if (socketHandle_ != INVALID_HANDLE_VALUE && shutdown_) shutdown_(socketHandle_, WINDIVERT_SHUTDOWN_RECV);
    if (networkHandle_ != INVALID_HANDLE_VALUE && shutdown_) shutdown_(networkHandle_, WINDIVERT_SHUTDOWN_RECV);
    flowThread = std::move(flowThread_);
    socketThread = std::move(socketThread_);
    networkThread = std::move(networkThread_);
  }
  if (flowThread.joinable()) flowThread.join();
  if (socketThread.joinable()) socketThread.join();
  if (networkThread.joinable()) networkThread.join();
  std::lock_guard lock(mutex_);
  DWORD closeError = ERROR_SUCCESS;
  if (flowHandle_ != INVALID_HANDLE_VALUE &&
      (!close_ || !close_(flowHandle_))) closeError = close_ ? GetLastError() : ERROR_PROC_NOT_FOUND;
  if (socketHandle_ != INVALID_HANDLE_VALUE &&
      (!close_ || !close_(socketHandle_)) && closeError == ERROR_SUCCESS)
    closeError = close_ ? GetLastError() : ERROR_PROC_NOT_FOUND;
  if (networkHandle_ != INVALID_HANDLE_VALUE &&
      (!close_ || !close_(networkHandle_)) && closeError == ERROR_SUCCESS)
    closeError = close_ ? GetLastError() : ERROR_PROC_NOT_FOUND;
  lastStopError_ = closeError;
  flowHandle_ = INVALID_HANDLE_VALUE;
  socketHandle_ = INVALID_HANDLE_VALUE;
  networkHandle_ = INVALID_HANDLE_VALUE;
  if (module_) FreeLibrary(module_);
  module_ = nullptr;
  open_ = nullptr; recv_ = nullptr; shutdown_ = nullptr; close_ = nullptr;
  onEvent_ = {};
  onError_ = {};
}

void WinDivertAdapter::receive(HANDLE handle, CaptureLayer layer) {
  std::array<UINT8,65575> packetBuffer{};
  for (;;) {
    WINDIVERT_ADDRESS address{};
    UINT packetLength = 0;
    const auto packet = layer == CaptureLayer::Network ? packetBuffer.data() : nullptr;
    const auto packetCapacity = layer == CaptureLayer::Network ? static_cast<UINT>(packetBuffer.size()) : 0;
    if (!recv_(handle, packet, packetCapacity, layer == CaptureLayer::Network ? &packetLength : nullptr, &address)) {
      const DWORD code = GetLastError();
      bool stopping = false;
      {
        std::lock_guard lock(mutex_);
        stopping = stopping_;
      }
      if (!stopping && code != ERROR_NO_DATA) reportError(code);
      return;
    }
    CaptureEvent event{};
    event.layer = layer;
    LARGE_INTEGER now{};QueryPerformanceCounter(&now);
    LARGE_INTEGER captureTime{};captureTime.QuadPart=address.Timestamp;
    const auto capturedNs=qpcNanoseconds(captureTime);
    event.timestampNs=capturedNs;
    event.captureLatencyNs=now.QuadPart>=address.Timestamp?qpcNanoseconds(now)-capturedNs:0;
    if (layer == CaptureLayer::Network) {
      if (packetLength == 0 || packetLength > packetBuffer.size()) { reportError(ERROR_INVALID_DATA); continue; }
      event.kind=CaptureEventKind::NetworkPacket;
      event.outbound=address.Outbound!=0;
      event.loopback=address.Loopback!=0;
      event.ipv4ChecksumValid=address.IPChecksum!=0;
      event.transportChecksumValid=address.TCPChecksum!=0||address.UDPChecksum!=0;
      event.packetBytes.assign(packetBuffer.begin(),packetBuffer.begin()+packetLength);
      if (onEvent_) onEvent_(std::move(event));
      continue;
    }
    event.kind = eventKind(layer, address.Event);
    if (event.kind == CaptureEventKind::Error) {
      reportError(ERROR_INVALID_DATA);
      continue;
    }
    if (event.kind == CaptureEventKind::Other) continue;
    if (layer == CaptureLayer::Flow) {
      event.endpointId = address.Flow.EndpointId;
      event.parentEndpointId = address.Flow.ParentEndpointId;
      event.processId = address.Flow.ProcessId;
      copyAddress(event.tuple, address.Flow.LocalAddr, address.Flow.RemoteAddr,
                  address.Flow.LocalPort, address.Flow.RemotePort, address.Flow.Protocol,
                  address.IPv6 != 0);
    } else {
      event.endpointId = address.Socket.EndpointId;
      event.parentEndpointId = address.Socket.ParentEndpointId;
      event.processId = address.Socket.ProcessId;
      copyAddress(event.tuple, address.Socket.LocalAddr, address.Socket.RemoteAddr,
                  address.Socket.LocalPort, address.Socket.RemotePort, address.Socket.Protocol,
                  address.IPv6 != 0);
    }
    if (onEvent_) onEvent_(std::move(event));
  }
}

void WinDivertAdapter::reportError(DWORD error) {
  if (onError_) onError_(error);
}

} // namespace steamdpi
