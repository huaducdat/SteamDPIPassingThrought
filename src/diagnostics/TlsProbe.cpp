#include "TlsProbe.h"
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winhttp.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <new>
#include <string>

namespace steamdpi {
namespace {
struct RequestContext {
  HANDLE completionEvent = nullptr;
  std::atomic<unsigned> references{1};
  std::mutex mutex;
  DWORD completionStatus = 0;
  DWORD error = ERROR_SUCCESS;
};

void releaseContext(RequestContext* context) {
  if (!context || context->references.fetch_sub(1) != 1) return;
  if (context->completionEvent) CloseHandle(context->completionEvent);
  delete context;
}

void CALLBACK requestCallback(HINTERNET, DWORD_PTR contextValue, DWORD status,
                              LPVOID statusInformation, DWORD) {
  auto* context = reinterpret_cast<RequestContext*>(contextValue);
  if (!context) return;

  if (status == WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING) {
    releaseContext(context);
    return;
  }

  if (status == WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE ||
      status == WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE) {
    {
      std::lock_guard lock(context->mutex);
      context->completionStatus = status;
      context->error = ERROR_SUCCESS;
    }
    SetEvent(context->completionEvent);
    return;
  }

  if (status == WINHTTP_CALLBACK_STATUS_REQUEST_ERROR) {
    const auto* operation = static_cast<const WINHTTP_ASYNC_RESULT*>(statusInformation);
    {
      std::lock_guard lock(context->mutex);
      context->completionStatus = status;
      context->error = operation ? operation->dwError : ERROR_WINHTTP_INTERNAL_ERROR;
    }
    SetEvent(context->completionEvent);
    return;
  }

  if (status == WINHTTP_CALLBACK_STATUS_SECURE_FAILURE) {
    {
      std::lock_guard lock(context->mutex);
      context->completionStatus = WINHTTP_CALLBACK_STATUS_REQUEST_ERROR;
      context->error = ERROR_WINHTTP_SECURE_FAILURE;
    }
    SetEvent(context->completionEvent);
  }
}

enum class WaitResult { Complete, Failed, Cancelled, TimedOut };

struct Completion {
  WaitResult result = WaitResult::Failed;
  DWORD error = ERROR_SUCCESS;
};

Completion awaitCompletion(RequestContext& context, DWORD expectedStatus,
                           std::chrono::steady_clock::time_point deadline,
                           const std::atomic_bool& cancel) {
  for (;;) {
    if (cancel.load()) return {WaitResult::Cancelled, ERROR_WINHTTP_OPERATION_CANCELLED};
    const auto now = std::chrono::steady_clock::now();
    if (now >= deadline) return {WaitResult::TimedOut, ERROR_WINHTTP_TIMEOUT};

    const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count();
    const DWORD slice = static_cast<DWORD>(std::max<long long>(1, std::min<long long>(remaining, 50)));
    const DWORD waitResult = WaitForSingleObject(context.completionEvent, slice);
    if (waitResult == WAIT_TIMEOUT) continue;
    if (waitResult != WAIT_OBJECT_0) return {WaitResult::Failed, GetLastError()};

    DWORD status = 0;
    DWORD error = ERROR_SUCCESS;
    {
      std::lock_guard lock(context.mutex);
      status = context.completionStatus;
      error = context.error;
      context.completionStatus = 0;
      context.error = ERROR_SUCCESS;
      ResetEvent(context.completionEvent);
    }
    if (status == expectedStatus) return {WaitResult::Complete, ERROR_SUCCESS};
    return {WaitResult::Failed, error};
  }
}

Completion sendRequest(HINTERNET request, RequestContext& context,
                       std::chrono::steady_clock::time_point deadline,
                       const std::atomic_bool& cancel) {
  if (cancel.load()) return {WaitResult::Cancelled, ERROR_WINHTTP_OPERATION_CANCELLED};
  if (WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                         WINHTTP_NO_REQUEST_DATA, 0, 0,
                         reinterpret_cast<DWORD_PTR>(&context))) {
    return {WaitResult::Complete, ERROR_SUCCESS};
  }
  const DWORD error = GetLastError();
  if (error != ERROR_IO_PENDING) return {WaitResult::Failed, error};
  return awaitCompletion(context, WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE, deadline, cancel);
}

Completion receiveResponse(HINTERNET request, RequestContext& context,
                           std::chrono::steady_clock::time_point deadline,
                           const std::atomic_bool& cancel) {
  if (cancel.load()) return {WaitResult::Cancelled, ERROR_WINHTTP_OPERATION_CANCELLED};
  if (WinHttpReceiveResponse(request, nullptr)) return {WaitResult::Complete, ERROR_SUCCESS};
  const DWORD error = GetLastError();
  if (error != ERROR_IO_PENDING) return {WaitResult::Failed, error};
  return awaitCompletion(context, WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE, deadline, cancel);
}
}

ProbeResult probeTls(const std::string& host, int timeoutMs, const std::atomic_bool& cancel,
                     unsigned short port) {
  ProbeResult result{host, "TLS", {}, ProbeStatus::Failure, 0,
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::system_clock::now().time_since_epoch()).count()};
  const auto begin = std::chrono::steady_clock::now();
  const auto finish = [&] {
    result.latencyMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - begin).count();
    return result;
  };
  if (cancel.load()) {
    result.status = ProbeStatus::Cancelled;
    result.detail = "Cancelled";
    return finish();
  }

  const int chars = MultiByteToWideChar(CP_UTF8, 0, host.c_str(), -1, nullptr, 0);
  if (chars <= 1) {
    result.detail = "Invalid hostname";
    return finish();
  }
  std::wstring wideHost(static_cast<std::size_t>(chars), L'\0');
  if (MultiByteToWideChar(CP_UTF8, 0, host.c_str(), -1, wideHost.data(), chars) == 0) {
    result.detail = "Invalid hostname";
    return finish();
  }
  wideHost.resize(static_cast<std::size_t>(chars - 1));

  HINTERNET session = WinHttpOpen(L"SteamDPI/0.1", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                  WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS,
                                  WINHTTP_FLAG_ASYNC);
  if (!session) {
    result.detail = "Unable to initialize WinHTTP";
    return finish();
  }

  // Bound each WinHTTP phase as well as the end-to-end wait. Windows rounds
  // sub-second phase timeouts up, so cleanup may finish after the nominal
  // deadline while still remaining bounded.
  const DWORD phaseTimeout = static_cast<DWORD>(std::max(1, timeoutMs / 4));
  WinHttpSetTimeouts(session, phaseTimeout, phaseTimeout, phaseTimeout, phaseTimeout);
  HINTERNET connection = WinHttpConnect(session, wideHost.c_str(),
                                        static_cast<INTERNET_PORT>(port), 0);
  HINTERNET request = connection
      ? WinHttpOpenRequest(connection, L"HEAD", L"/", nullptr, WINHTTP_NO_REFERER,
                           WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE)
      : nullptr;
  if (!request) {
    const DWORD error = GetLastError();
    WinHttpCloseHandle(connection ? connection : session);
    if (connection) WinHttpCloseHandle(session);
    result.detail = "Unable to create TLS request (" + std::to_string(error) + ")";
    return finish();
  }

  auto* context = new (std::nothrow) RequestContext;
  if (context) context->completionEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  if (!context || !context->completionEvent) {
    const DWORD error = GetLastError();
    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connection);
    WinHttpCloseHandle(session);
    releaseContext(context);
    result.detail = "Unable to create TLS completion event (" + std::to_string(error) + ")";
    return finish();
  }

  DWORD_PTR contextValue = reinterpret_cast<DWORD_PTR>(context);
  const bool hasContext = WinHttpSetOption(request, WINHTTP_OPTION_CONTEXT_VALUE,
                                            &contextValue, sizeof(contextValue)) != FALSE;
  const DWORD notifications = WINHTTP_CALLBACK_FLAG_SEND_REQUEST |
                              WINHTTP_CALLBACK_FLAG_HEADERS_AVAILABLE |
                              WINHTTP_CALLBACK_FLAG_REQUEST_ERROR |
                              WINHTTP_CALLBACK_FLAG_SECURE_FAILURE |
                              WINHTTP_CALLBACK_FLAG_HANDLES;
  SetLastError(ERROR_SUCCESS);
  WinHttpSetStatusCallback(request, requestCallback, notifications, 0);
  const bool callbackInstalled = GetLastError() == ERROR_SUCCESS;
  const bool callbackOwnsContext = hasContext && callbackInstalled;
  if (callbackOwnsContext) context->references.fetch_add(1);

  Completion completion{WaitResult::Failed, ERROR_WINHTTP_INTERNAL_ERROR};
  if (!hasContext || !callbackInstalled) {
    completion.error = GetLastError();
  } else {
    const auto deadline = begin + std::chrono::milliseconds(std::max(1, timeoutMs));
    completion = sendRequest(request, *context, deadline, cancel);
    if (completion.result == WaitResult::Complete) {
      completion = receiveResponse(request, *context, deadline, cancel);
    }
  }

  if (completion.result == WaitResult::Complete) {
    result.status = ProbeStatus::Success;
    result.detail = "TLS handshake and certificate/hostname validation succeeded";
  } else if (completion.result == WaitResult::Cancelled || cancel.load()) {
    result.status = ProbeStatus::Cancelled;
    result.detail = "Cancelled";
  } else if (completion.result == WaitResult::TimedOut ||
             completion.error == ERROR_WINHTTP_TIMEOUT) {
    result.status = ProbeStatus::Timeout;
    result.detail = "TLS operation timed out";
  } else if (completion.error == ERROR_WINHTTP_SECURE_FAILURE) {
    result.detail = "TLS certificate or hostname validation failed";
  } else if (completion.error == ERROR_WINHTTP_CANNOT_CONNECT ||
             completion.error == ERROR_WINHTTP_NAME_NOT_RESOLVED ||
             completion.error == ERROR_WINHTTP_CONNECTION_ERROR) {
    result.detail = "TLS connection failed (" + std::to_string(completion.error) + ")";
  } else {
    result.detail = "Unknown TLS error (" + std::to_string(completion.error) + ")";
  }

  WinHttpCloseHandle(request);
  WinHttpCloseHandle(connection);
  WinHttpCloseHandle(session);
  releaseContext(context);
  return finish();
}
}
