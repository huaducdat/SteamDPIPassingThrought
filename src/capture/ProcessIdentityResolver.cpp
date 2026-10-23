#include "ProcessIdentityResolver.h"
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <wincrypt.h>
#include <softpub.h>
#include <wintrust.h>
#include <algorithm>
#include <array>
#include <cwctype>
#include <filesystem>
#include <vector>

#pragma comment(lib, "wintrust.lib")
#pragma comment(lib, "crypt32.lib")

namespace steamdpi {
namespace {
std::wstring lower(std::wstring value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
  return value;
}

std::wstring normalizePath(std::wstring path) {
  std::replace(path.begin(), path.end(), L'/', L'\\');
  if (path.rfind(L"\\\\?\\", 0) == 0) path.erase(0, 4);
  while (path.size() > 3 && path.back() == L'\\') path.pop_back();
  return lower(std::move(path));
}

std::wstring finalPath(const std::wstring& path, bool directory) {
  HANDLE file = CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES,
                            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                            nullptr, OPEN_EXISTING,
                            directory ? FILE_FLAG_BACKUP_SEMANTICS : FILE_ATTRIBUTE_NORMAL,
                            nullptr);
  if (file == INVALID_HANDLE_VALUE) return {};
  const DWORD required = GetFinalPathNameByHandleW(file, nullptr, 0, FILE_NAME_NORMALIZED);
  std::wstring result(required, L'\0');
  const DWORD length = required ? GetFinalPathNameByHandleW(file, result.data(), required,
                                                             FILE_NAME_NORMALIZED) : 0;
  CloseHandle(file);
  if (!length || length >= required) return {};
  result.resize(length);
  return result;
}

void addRegistryPath(std::vector<std::wstring>& roots, HKEY key, const wchar_t* subkey,
                     const wchar_t* value) {
  DWORD type = 0;
  DWORD bytes = 0;
  if (RegGetValueW(key, subkey, value, RRF_RT_REG_SZ, &type, nullptr, &bytes) != ERROR_SUCCESS ||
      bytes < sizeof(wchar_t)) return;
  std::wstring path(bytes / sizeof(wchar_t), L'\0');
  if (RegGetValueW(key, subkey, value, RRF_RT_REG_SZ, &type, path.data(), &bytes) != ERROR_SUCCESS)
    return;
  while (!path.empty() && path.back() == L'\0') path.pop_back();
  const auto canonical = finalPath(path, true);
  if (!canonical.empty() && std::none_of(roots.begin(), roots.end(), [&](const auto& existing) {
        return normalizePath(existing) == normalizePath(canonical);
      })) roots.push_back(canonical);
}

std::vector<std::wstring> registryRoots() {
  std::vector<std::wstring> roots;
  addRegistryPath(roots, HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamPath");
  addRegistryPath(roots, HKEY_LOCAL_MACHINE, L"SOFTWARE\\Valve\\Steam", L"InstallPath");
  addRegistryPath(roots, HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\Valve\\Steam",
                  L"InstallPath");
  return roots;
}

bool isValveSigned(const std::wstring& imagePath) {
  WINTRUST_FILE_INFO file{};
  file.cbStruct = sizeof(file);
  file.pcwszFilePath = imagePath.c_str();
  WINTRUST_DATA trust{};
  trust.cbStruct = sizeof(trust);
  trust.dwUIChoice = WTD_UI_NONE;
  trust.fdwRevocationChecks = WTD_REVOKE_NONE;
  trust.dwUnionChoice = WTD_CHOICE_FILE;
  trust.pFile = &file;
  trust.dwStateAction = WTD_STATEACTION_VERIFY;
  trust.dwProvFlags = WTD_CACHE_ONLY_URL_RETRIEVAL;
  GUID policy = WINTRUST_ACTION_GENERIC_VERIFY_V2;
  const LONG status = WinVerifyTrust(nullptr, &policy, &trust);
  trust.dwStateAction = WTD_STATEACTION_CLOSE;
  WinVerifyTrust(nullptr, &policy, &trust);
  if (status != ERROR_SUCCESS) return false;

  HCERTSTORE store = nullptr;
  HCRYPTMSG message = nullptr;
  DWORD encoding = 0, content = 0, format = 0;
  if (!CryptQueryObject(CERT_QUERY_OBJECT_FILE, imagePath.c_str(),
                        CERT_QUERY_CONTENT_FLAG_PKCS7_SIGNED_EMBED,
                        CERT_QUERY_FORMAT_FLAG_BINARY, 0, &encoding, &content, &format,
                        &store, &message, nullptr)) return false;
  DWORD signerBytes = 0;
  bool valve = false;
  if (CryptMsgGetParam(message, CMSG_SIGNER_INFO_PARAM, 0, nullptr, &signerBytes)) {
    std::vector<BYTE> buffer(signerBytes);
    auto* signer = reinterpret_cast<CMSG_SIGNER_INFO*>(buffer.data());
    if (CryptMsgGetParam(message, CMSG_SIGNER_INFO_PARAM, 0, signer, &signerBytes)) {
      CERT_INFO find{};
      find.Issuer = signer->Issuer;
      find.SerialNumber = signer->SerialNumber;
      PCCERT_CONTEXT cert = CertFindCertificateInStore(store, encoding,
          0, CERT_FIND_SUBJECT_CERT, &find, nullptr);
      if (cert) {
        const DWORD chars = CertGetNameStringW(cert, CERT_NAME_ATTR_TYPE, 0,
                                               const_cast<LPSTR>(szOID_ORGANIZATION_NAME),
                                               nullptr, 0);
        std::wstring organization(chars ? chars : 1, L'\0');
        if (chars > 1 && CertGetNameStringW(cert, CERT_NAME_ATTR_TYPE, 0,
              const_cast<LPSTR>(szOID_ORGANIZATION_NAME), organization.data(), chars)) {
          organization.resize(chars - 1);
          const auto normalized = lower(organization);
          valve = normalized == L"valve corp." || normalized == L"valve corporation";
        }
        CertFreeCertificateContext(cert);
      }
    }
  }
  if (message) CryptMsgClose(message);
  if (store) CertCloseStore(store, 0);
  return valve;
}
}

ProcessIdentityResolver::ProcessIdentityResolver() : roots_(registryRoots()) {}
ProcessIdentityResolver::ProcessIdentityResolver(std::vector<std::wstring> steamRoots)
    : roots_(std::move(steamRoots)) {}

bool ProcessIdentityResolver::isVerifiedSteamImagePath(
    const std::wstring& imagePath, const std::vector<std::wstring>& steamRoots,
    bool trustedValveSignature) {
  if (!trustedValveSignature) return false;
  const auto image = normalizePath(imagePath);
  const auto filename = std::filesystem::path(image).filename().wstring();
  static const std::array<std::wstring, 2> allowed{L"steam.exe", L"steamwebhelper.exe"};
  if (std::find(allowed.begin(), allowed.end(), filename) == allowed.end()) return false;
  for (const auto& rootPath : steamRoots) {
    const auto root = normalizePath(rootPath);
    if (image.size() > root.size() && image.compare(0, root.size(), root) == 0 &&
        image[root.size()] == L'\\') return true;
  }
  return false;
}

std::optional<ProcessIdentity> ProcessIdentityResolver::resolve(std::uint32_t processId) {
  if (processId == 0 || roots_.empty()) return std::nullopt;
  HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
  if (!process) return std::nullopt;
  FILETIME created{}, exited{}, kernel{}, user{};
  if (!GetProcessTimes(process, &created, &exited, &kernel, &user)) {
    CloseHandle(process);
    return std::nullopt;
  }
  const std::uint64_t creation = (static_cast<std::uint64_t>(created.dwHighDateTime) << 32) |
                                 created.dwLowDateTime;
  {
    std::lock_guard lock(mutex_);
    const auto it = cache_.find(processId);
    if (it != cache_.end() && it->second.creationTime == creation) {
      CloseHandle(process);
      return it->second.identity;
    }
  }
  std::wstring image(32768, L'\0');
  DWORD chars = static_cast<DWORD>(image.size());
  std::optional<ProcessIdentity> result;
  if (QueryFullProcessImageNameW(process, 0, image.data(), &chars)) {
    image.resize(chars);
    const auto resolvedPath = finalPath(image, false);
    if (!resolvedPath.empty() &&
        isVerifiedSteamImagePath(resolvedPath, roots_, isValveSigned(resolvedPath)))
      result = ProcessIdentity{processId, resolvedPath, true, creation};
  }
  CloseHandle(process);
  {
    std::lock_guard lock(mutex_);
    if (cache_.size() >= 2048) cache_.clear();
    cache_[processId] = CacheEntry{creation, result};
  }
  return result;
}

} // namespace steamdpi
