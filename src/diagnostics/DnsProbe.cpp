#include "DnsProbe.h"
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <chrono>
#include <algorithm>
#include <ctime>
#include <cstring>

namespace steamdpi {
ProbeResult probeDns(const std::string& host, int timeoutMs, const std::atomic_bool& cancel) {
  ProbeResult result{host, "DNS", {}, ProbeStatus::Failure, 0,
      std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count()};
  if (cancel.load()) { result.status = ProbeStatus::Cancelled; result.detail = "Cancelled"; return result; }
  WSADATA wsa{};
  if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) { result.detail = "Winsock initialization failed"; return result; }

  wchar_t name[256]{};
  if (MultiByteToWideChar(CP_UTF8, 0, host.c_str(), -1, name, 256) == 0) {
    result.detail = "Invalid hostname"; WSACleanup(); return result;
  }
  OVERLAPPED overlapped{};
  overlapped.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  if (!overlapped.hEvent) { result.detail = "Unable to create DNS completion event"; WSACleanup(); return result; }

  PADDRINFOEXW addresses = nullptr;
  HANDLE queryHandle = nullptr;
  const auto start = std::chrono::steady_clock::now();
  const int rc = GetAddrInfoExW(name, nullptr, NS_DNS, nullptr, nullptr, &addresses,
                                nullptr, &overlapped, nullptr, &queryHandle);
  DWORD wait = WAIT_OBJECT_0;
  if (rc == WSA_IO_PENDING) {
    const auto deadline = start + std::chrono::milliseconds(timeoutMs);
    do {
      const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
      wait = WaitForSingleObject(overlapped.hEvent, static_cast<DWORD>(std::max<long long>(0, std::min<long long>(remaining, 100))));
    } while (wait == WAIT_TIMEOUT && !cancel.load() && std::chrono::steady_clock::now() < deadline);
  }

  using CancelFn = int (WSAAPI*)(HANDLE*);
  using ResultFn = int (WSAAPI*)(LPOVERLAPPED);
  const auto module = GetModuleHandleW(L"ws2_32.dll");
  const auto cancelAddress = GetProcAddress(module, "GetAddrInfoExCancel");
  const auto resultAddress = GetProcAddress(module, "GetAddrInfoExOverlappedResult");
  CancelFn cancelFn{};
  ResultFn resultFn{};
  static_assert(sizeof(cancelFn) == sizeof(cancelAddress) && sizeof(resultFn) == sizeof(resultAddress));
  std::memcpy(&cancelFn, &cancelAddress, sizeof(cancelFn));
  std::memcpy(&resultFn, &resultAddress, sizeof(resultFn));
  if (rc == WSA_IO_PENDING && (wait == WAIT_TIMEOUT || cancel.load())) {
    if (queryHandle && cancelFn) cancelFn(&queryHandle);
    WaitForSingleObject(overlapped.hEvent, INFINITE);
    result.status = cancel.load() ? ProbeStatus::Cancelled : ProbeStatus::Timeout;
    result.detail = cancel.load() ? "Cancelled" : "DNS resolution timed out";
  } else {
    const int error = rc == WSA_IO_PENDING ? (resultFn ? resultFn(&overlapped) : WSA_OPERATION_ABORTED) : rc;
    result.latencyMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();
    if (error == 0 && addresses) {
      unsigned count = 0;
      for (auto item = addresses; item; item = reinterpret_cast<PADDRINFOEXW>(item->ai_next)) ++count;
      result.status = ProbeStatus::Success;
      result.detail = "Resolved A/AAAA records (" + std::to_string(count) + ")";
    } else {
      result.detail = "DNS resolution failed (" + std::to_string(error) + ")";
    }
  }
  if (result.latencyMs == 0) result.latencyMs = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - start).count();
  if (addresses) FreeAddrInfoExW(addresses);
  CloseHandle(overlapped.hEvent);
  WSACleanup();
  return result;
}
}
