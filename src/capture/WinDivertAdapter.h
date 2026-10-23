#pragma once

#include "CaptureAdapter.h"
#include <windows.h>
#include <windivert.h>
#include <thread>
#include <mutex>
#include <atomic>

namespace steamdpi {

class WinDivertAdapter final : public CaptureAdapter {
public:
  WinDivertAdapter() = default;
  ~WinDivertAdapter() override;
  bool start(EventCallback onEvent, ErrorCallback onError, std::string& error) override;
  void stop() override;
  std::uint32_t lastStopError() const override { return lastStopError_.load(); }

private:
  void receive(HANDLE handle, CaptureLayer layer);
  void reportError(DWORD error);

  std::mutex mutex_;
  HANDLE flowHandle_ = INVALID_HANDLE_VALUE;
  HANDLE socketHandle_ = INVALID_HANDLE_VALUE;
  HANDLE networkHandle_ = INVALID_HANDLE_VALUE;
  std::thread flowThread_;
  std::thread socketThread_;
  std::thread networkThread_;
  EventCallback onEvent_;
  ErrorCallback onError_;
  bool stopping_ = false;
  std::atomic<std::uint32_t> lastStopError_{0};
  HMODULE module_ = nullptr;
  decltype(&WinDivertOpen) open_ = nullptr;
  decltype(&WinDivertRecv) recv_ = nullptr;
  decltype(&WinDivertShutdown) shutdown_ = nullptr;
  decltype(&WinDivertClose) close_ = nullptr;
};

} // namespace steamdpi
