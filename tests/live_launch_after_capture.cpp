#include "capture/CaptureController.h"
#include "capture/ProcessIdentityResolver.h"
#include <windows.h>
#include <tlhelp32.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>

namespace {
struct SteamProcesses {
  unsigned clients = 0;
  unsigned webHelpers = 0;
  bool any() const { return clients != 0 || webHelpers != 0; }
};

SteamProcesses discoverSteam(steamdpi::ProcessIdentityResolver& resolver) {
  SteamProcesses found;
  HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snapshot == INVALID_HANDLE_VALUE) return found;
  PROCESSENTRY32W entry{};
  entry.dwSize = sizeof(entry);
  if (Process32FirstW(snapshot, &entry)) {
    do {
      const auto identity = resolver.resolve(entry.th32ProcessID);
      if (!identity) continue;
      const auto filename = std::filesystem::path(identity->imagePath).filename().wstring();
      if (_wcsicmp(filename.c_str(), L"steam.exe") == 0) ++found.clients;
      if (_wcsicmp(filename.c_str(), L"steamwebhelper.exe") == 0) ++found.webHelpers;
    } while (Process32NextW(snapshot, &entry));
  }
  CloseHandle(snapshot);
  return found;
}

}

int main(int argc, char** argv) {
  if (argc == 0) return 1;
  const auto logPath = std::filesystem::path(argv[0]).parent_path() /
                       "live_launch_after_capture.log";
  std::ofstream log(logPath, std::ios::trunc);
  const auto write = [&log](const std::string& line) {
    std::cout << line << '\n' << std::flush;
    if (log) log << line << '\n' << std::flush;
  };
  if (!steamdpi::CaptureController::isAdministrator()) {
    write("FAIL: elevated token required");
    return 2;
  }

  int result = 0;
  {
    steamdpi::CaptureController capture;
    std::string error;
    if (!capture.start(error) ||
        capture.snapshot().state != steamdpi::CaptureState::Capturing) {
      write("FAIL: observation did not become active: " + error);
      return 3;
    }
    write("CAPTURE_ACTIVE");

    steamdpi::ProcessIdentityResolver resolver;
    auto processes = discoverSteam(resolver);
    if (processes.any()) {
      write("SKIPPED: Steam was already running; it was left running unchanged");
    } else {
      write("SKIPPED: Steam is not running; this elevated helper will not launch Steam with an Administrator token");
    }
    capture.stop();
    if (capture.snapshot().state != steamdpi::CaptureState::Stopped) {
      write("FAIL: capture did not stop normally");
      result = 6;
    } else {
      write("CAPTURE_STOPPED");
    }
  }
  return result;
}
