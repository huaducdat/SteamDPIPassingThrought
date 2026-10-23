cmake_minimum_required(VERSION 3.25)
project(SteamDPI VERSION 0.1.0 LANGUAGES CXX)

if(NOT WIN32)
  message(FATAL_ERROR "SteamDPI currently supports Windows only.")
endif()
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
option(STEAMDPI_BUILD_GUI "Build the Qt desktop application" ON)
option(STEAMDPI_BUILD_TESTS "Build tests" ON)

add_library(steamdpi_core
  src/diagnostics/DiagnosticService.cpp
  src/diagnostics/DnsProbe.cpp src/diagnostics/TcpProbe.cpp
  src/diagnostics/TlsProbe.cpp src/network/SteamTargets.cpp
  src/capture/CaptureController.cpp src/capture/FlowTracker.cpp
  src/capture/ProcessIdentityResolver.cpp src/capture/SteamTrafficClassifier.cpp
  src/capture/WinDivertAdapter.cpp)
target_include_directories(steamdpi_core PUBLIC src)

# Offline-only packet laboratory. Keep this target free of Qt, WinDivert,
# Winsock, and driver lifecycle dependencies.
add_library(steamdpi_packet_lab
  src/packet/PacketParser.cpp
  src/packet/PacketTransformPipeline.cpp
  src/packet/PacketFixtureLoader.cpp
  src/packet/PacketReplayRunner.cpp)
target_include_directories(steamdpi_packet_lab PUBLIC src)
target_link_libraries(steamdpi_core PUBLIC steamdpi_packet_lab)
if(MSVC)
  target_compile_options(steamdpi_packet_lab PRIVATE /W4 /permissive- /utf-8)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(steamdpi_packet_lab PRIVATE -Wall -Wextra -Wpedantic)
endif()
set(STEAMDPI_WINDIVERT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/windivert/2.2.1")
if(NOT EXISTS "${STEAMDPI_WINDIVERT_ROOT}/include/windivert.h" OR
   NOT EXISTS "${STEAMDPI_WINDIVERT_ROOT}/x64/WinDivert.dll" OR
   NOT EXISTS "${STEAMDPI_WINDIVERT_ROOT}/x64/WinDivert64.sys")
  message(FATAL_ERROR "Pinned WinDivert 2.2.1 x64 files are missing")
endif()
file(READ "${STEAMDPI_WINDIVERT_ROOT}/VERSION" STEAMDPI_WINDIVERT_VERSION)
string(STRIP "${STEAMDPI_WINDIVERT_VERSION}" STEAMDPI_WINDIVERT_VERSION)
file(SHA256 "${STEAMDPI_WINDIVERT_ROOT}/include/windivert.h" STEAMDPI_WINDIVERT_HEADER_SHA256)
file(SHA256 "${STEAMDPI_WINDIVERT_ROOT}/x64/WinDivert.dll" STEAMDPI_WINDIVERT_DLL_SHA256)
file(SHA256 "${STEAMDPI_WINDIVERT_ROOT}/x64/WinDivert64.sys" STEAMDPI_WINDIVERT_SYS_SHA256)
string(TOUPPER "${STEAMDPI_WINDIVERT_HEADER_SHA256}" STEAMDPI_WINDIVERT_HEADER_SHA256)
string(TOUPPER "${STEAMDPI_WINDIVERT_DLL_SHA256}" STEAMDPI_WINDIVERT_DLL_SHA256)
string(TOUPPER "${STEAMDPI_WINDIVERT_SYS_SHA256}" STEAMDPI_WINDIVERT_SYS_SHA256)
if(NOT STEAMDPI_WINDIVERT_VERSION STREQUAL "2.2.1" OR
   NOT STEAMDPI_WINDIVERT_HEADER_SHA256 STREQUAL "5017A1768C1592FD664C0C2D3D2D30F81FAD4AB98D322B1914C6F0A33FCACDF9" OR
   NOT STEAMDPI_WINDIVERT_DLL_SHA256 STREQUAL "4B0BDD875D02DF084D87D1573BCECF393D67DA4AB3DDCCE725AF2F477B90D87E" OR
   NOT STEAMDPI_WINDIVERT_SYS_SHA256 STREQUAL "8248306BCC5FAE20FD4F3D5C44F962C85CDDBE020B34A1799350CE2034154B7D")
  message(FATAL_ERROR "WinDivert files differ from the inspected 2.2.1 x64 pin")
endif()
target_include_directories(steamdpi_core PRIVATE "${STEAMDPI_WINDIVERT_ROOT}/include")
target_link_libraries(steamdpi_core PUBLIC advapi32 wintrust crypt32)
target_compile_definitions(steamdpi_core PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
target_link_libraries(steamdpi_core PUBLIC ws2_32 dnsapi winhttp)
if(MSVC)
  target_compile_options(steamdpi_core PRIVATE /W4 /permissive- /utf-8)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(steamdpi_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(STEAMDPI_BUILD_GUI)
  find_package(Qt6 6.12 REQUIRED COMPONENTS Widgets)
  set(CMAKE_AUTOMOC ON)
  qt_add_executable(SteamDPI WIN32 src/app/main.cpp src/app/MainWindow.cpp src/app/MainWindow.h src/core/AppController.cpp src/core/AppConfig.cpp src/core/ReportSerializer.cpp)
  target_link_libraries(SteamDPI PRIVATE steamdpi_core Qt6::Widgets)
  target_compile_definitions(SteamDPI PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
  if(MSVC)
    target_compile_options(SteamDPI PRIVATE /W4 /permissive- /utf-8)
  endif()
  add_custom_command(TARGET SteamDPI POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${STEAMDPI_WINDIVERT_ROOT}/x64/WinDivert.dll"
      "${STEAMDPI_WINDIVERT_ROOT}/x64/WinDivert64.sys"
      "$<TARGET_FILE_DIR:SteamDPI>"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${STEAMDPI_WINDIVERT_ROOT}/LICENSE" "$<TARGET_FILE_DIR:SteamDPI>/WinDivert-LICENSE.txt")
endif()

include(CTest)
if(BUILD_TESTING AND STEAMDPI_BUILD_TESTS)
  add_subdirectory(tests)
endif()
