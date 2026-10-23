#include "capture/ProcessIdentityResolver.h"
#include <cerrno>
#include <cstdlib>
#include <iostream>

int wmain(int argc, wchar_t** argv) {
  if (argc != 2) {
    std::wcerr << L"Usage: steamdpi_capture_identity_probe.exe <pid>\n";
    return 2;
  }
  wchar_t* end = nullptr;
  errno = 0;
  const auto pid = std::wcstoul(argv[1], &end, 10);
  if (errno || !end || *end || pid == 0 || pid > 0xffffffffUL) {
    std::wcerr << L"Invalid PID\n";
    return 2;
  }
  steamdpi::ProcessIdentityResolver resolver;
  const auto identity = resolver.resolve(static_cast<std::uint32_t>(pid));
  if (!identity) {
    std::wcerr << L"Process was not attributed to a verified Steam executable\n";
    return 1;
  }
  std::wcout << L"Verified Steam executable path: " << identity->imagePath << L"\n";
  return 0;
}
