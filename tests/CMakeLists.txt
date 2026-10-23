add_executable(steamdpi_tests test_main.cpp)
target_link_libraries(steamdpi_tests PRIVATE steamdpi_core)
add_executable(steamdpi_packet_lab_tests packet_lab_test.cpp)
target_link_libraries(steamdpi_packet_lab_tests PRIVATE steamdpi_packet_lab)
add_executable(steamdpi_packet_lab_benchmark packet_lab_benchmark.cpp)
target_link_libraries(steamdpi_packet_lab_benchmark PRIVATE steamdpi_packet_lab)
add_executable(steamdpi_live_diagnostics steam_diagnostics.cpp)
target_link_libraries(steamdpi_live_diagnostics PRIVATE steamdpi_core)
add_executable(steamdpi_capture_identity_probe capture_identity_probe.cpp)
target_link_libraries(steamdpi_capture_identity_probe PRIVATE steamdpi_core)
add_executable(steamdpi_live_capture_acceptance live_capture_acceptance.cpp)
target_link_libraries(steamdpi_live_capture_acceptance PRIVATE steamdpi_core psapi)
add_executable(steamdpi_live_launch_after_capture live_launch_after_capture.cpp)
target_link_libraries(steamdpi_live_launch_after_capture PRIVATE steamdpi_core)
set_target_properties(steamdpi_live_diagnostics PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>/tests")
set_target_properties(steamdpi_live_capture_acceptance PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>/tests")
set_target_properties(steamdpi_live_launch_after_capture PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>/tests")
add_custom_command(TARGET steamdpi_live_capture_acceptance POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${STEAMDPI_WINDIVERT_ROOT}/x64/WinDivert.dll"
    "${STEAMDPI_WINDIVERT_ROOT}/x64/WinDivert64.sys"
    "$<TARGET_FILE_DIR:steamdpi_live_capture_acceptance>")
add_custom_command(TARGET steamdpi_live_launch_after_capture POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${STEAMDPI_WINDIVERT_ROOT}/x64/WinDivert.dll"
    "${STEAMDPI_WINDIVERT_ROOT}/x64/WinDivert64.sys"
    "$<TARGET_FILE_DIR:steamdpi_live_launch_after_capture>")
if(MSVC)
  target_compile_options(steamdpi_tests PRIVATE /W4 /permissive- /utf-8)
  target_compile_options(steamdpi_live_diagnostics PRIVATE /W4 /permissive- /utf-8)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(steamdpi_tests PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(steamdpi_live_diagnostics PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_test(NAME steamdpi_tests COMMAND steamdpi_tests)
add_test(NAME steamdpi_packet_lab_tests COMMAND steamdpi_packet_lab_tests)
if(STEAMDPI_BUILD_GUI)
  add_executable(steamdpi_config_tests config_test.cpp ../src/core/AppConfig.cpp ../src/core/ReportSerializer.cpp)
  target_include_directories(steamdpi_config_tests PRIVATE ../src)
  target_link_libraries(steamdpi_config_tests PRIVATE Qt6::Core)
  add_test(NAME steamdpi_config_tests COMMAND steamdpi_config_tests)
  set_tests_properties(steamdpi_config_tests PROPERTIES
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
  )
endif()
