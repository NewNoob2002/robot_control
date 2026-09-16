cmake_minimum_required(VERSION 3.22)

project(
  robot_control
  VERSION 0.1.0
  DESCRIPTION "RK3588 Linux motion-control middleware"
  LANGUAGES C CXX)

include(CTest)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(ROBOT_CONTROL_WARNINGS_AS_ERRORS "Treat project warnings as errors" ON)
option(ROBOT_CONTROL_BUILD_CANOPEN_COMMISSIONING
       "Build the Debug-only read-only CANopen commissioning tool" OFF)
option(ROBOT_CONTROL_BUILD_ZLAC_QUALIFICATION
       "Build the Debug-only bounded ZLAC qualification tool" OFF)
option(ROBOT_CONTROL_BUILD_CANOPEN_RUNTIME
       "Build the Debug-only PDO runtime library and virtual-bus checks" OFF)
set(canopen_write_modes 0)
foreach(mode ROBOT_CONTROL_BUILD_CANOPEN_COMMISSIONING
             ROBOT_CONTROL_BUILD_ZLAC_QUALIFICATION ROBOT_CONTROL_BUILD_CANOPEN_RUNTIME)
  if(${mode})
    math(EXPR canopen_write_modes "${canopen_write_modes} + 1")
  endif()
endforeach()
if(canopen_write_modes GREATER 1)
  message(FATAL_ERROR "CANopen commissioning, qualification and runtime are mutually exclusive")
endif()
if(canopen_write_modes GREATER 0)
  if(CMAKE_CONFIGURATION_TYPES OR NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
    message(FATAL_ERROR "CANopen commissioning/qualification/runtime requires a single-config Debug build")
  endif()
endif()

add_library(robot_control_project_options INTERFACE)
target_compile_features(robot_control_project_options INTERFACE cxx_std_20)

add_library(robot_control_project_warnings INTERFACE)
target_compile_options(
  robot_control_project_warnings
  INTERFACE
    -Wall
    -Wextra
    -Wpedantic
    -Wconversion
    -Wsign-conversion
    -Wshadow
    -Wformat=2
    -Wundef)
if(ROBOT_CONTROL_WARNINGS_AS_ERRORS)
  target_compile_options(robot_control_project_warnings INTERFACE -Werror)
endif()

add_subdirectory(domain)
add_subdirectory(input/sbus/protocol)
add_subdirectory(input/sbus/source)
add_subdirectory(platform)
add_subdirectory(input/sbus/linux)
add_subdirectory(service)
add_subdirectory(communication/canopen)
add_subdirectory(tools/can_probe)
add_subdirectory(tools/canopen_observer)
add_subdirectory(tools/platform_probe)
add_subdirectory(tools/sbus_observer)
if(ROBOT_CONTROL_BUILD_CANOPEN_COMMISSIONING)
  add_subdirectory(tools/canopen_commission)
endif()
if(ROBOT_CONTROL_BUILD_ZLAC_QUALIFICATION)
  add_subdirectory(tools/can_interface_inhibitor)
  add_subdirectory(tools/zlac_qualification)
endif()

# This pure contract executable can also be staged for a no-device target smoke.
if(BUILD_TESTING)
  add_executable(robot_control_sbus_source_tests tests/unit/sbus_source_tests.cpp)
  target_link_libraries(robot_control_sbus_source_tests
    PRIVATE robot_control_input_sbus_source robot_control_project_warnings)
  if(NOT CMAKE_CROSSCOMPILING)
    add_test(NAME sbus_source_contract COMMAND robot_control_sbus_source_tests)
  endif()
endif()

# These contract/link fixtures are also cross-built; CTest registration is host-only.
if(BUILD_TESTING)
  add_executable(robot_control_canopen_runtime_policy_tests tests/unit/canopen_runtime_policy_tests.cpp)
  target_link_libraries(robot_control_canopen_runtime_policy_tests
    PRIVATE robot_control_domain_drive robot_control_project_warnings)
  if(NOT CMAKE_CROSSCOMPILING)
    add_test(NAME canopen_runtime_policy COMMAND robot_control_canopen_runtime_policy_tests)
  endif()
  if(ROBOT_CONTROL_BUILD_CANOPEN_RUNTIME)
    add_executable(robot_control_canopen_runtime_vcan_tests
      tests/unit/canopen_runtime_vcan_tests.cpp tests/unit/canopen_test_log_stub.c)
    target_link_libraries(robot_control_canopen_runtime_vcan_tests
      PRIVATE robot_control_communication_canopen_runtime robot_control_platform_linux robot_control_project_warnings)
    target_link_options(robot_control_canopen_runtime_vcan_tests PRIVATE "-Wl,--wrap=send")
    if(NOT CMAKE_CROSSCOMPILING)
      add_test(NAME canopen_runtime_vcan_managed
        COMMAND bash ${PROJECT_SOURCE_DIR}/scripts/test/test_socketcan_vcan.sh
          $<TARGET_FILE:robot_control_canopen_runtime_vcan_tests>)
      set_tests_properties(canopen_runtime_vcan_managed PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 15)
    endif()
  endif()
endif()

if(BUILD_TESTING AND NOT CMAKE_CROSSCOMPILING)
  # Register an invalid can_probe invocation that must return usage error 2.
  function(add_can_probe_invalid_cli_test test_name)
    add_test(
      NAME ${test_name}
      COMMAND
        ${CMAKE_COMMAND}
        "-DPROGRAM=$<TARGET_FILE:robot-control-can-probe>"
        "-DPROGRAM_ARGUMENTS=${ARGN}" -DEXPECTED_EXIT_CODE=2 -P
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/assert_exit_code.cmake")
  endfunction()

  add_subdirectory(tests/unit)
  add_subdirectory(tests/integration)
  add_test(NAME platform_probe_help COMMAND robot-control-platform-probe --help)
  add_test(NAME platform_probe_run COMMAND robot-control-platform-probe)
  add_test(NAME platform_probe_phase3_smoke
           COMMAND robot-control-platform-probe --phase3-smoke)
  add_test(NAME can_probe_help COMMAND robot-control-can-probe --help)
  add_can_probe_invalid_cli_test(can_probe_missing_interface)
  add_can_probe_invalid_cli_test(can_probe_unknown_argument --unknown)
  add_can_probe_invalid_cli_test(can_probe_duration_zero --interface unused0
                                 --duration-ms 0)
  add_can_probe_invalid_cli_test(can_probe_duration_nonnumeric --interface
                                 unused0 --duration-ms abc)
  add_can_probe_invalid_cli_test(
    can_probe_duration_overflow --interface unused0 --duration-ms
    9999999999999999999999999999999999999999)
  add_can_probe_invalid_cli_test(can_probe_duplicate_interface --interface
                                 unused0 --interface unused1)
  add_can_probe_invalid_cli_test(can_probe_duplicate_duration --interface
                                 unused0 --duration-ms 1 --duration-ms 2)
  add_can_probe_invalid_cli_test(can_probe_interface_missing_value --interface
                                 --duration-ms 1)
  add_can_probe_invalid_cli_test(can_probe_duration_missing_value --interface
                                 unused0 --duration-ms)
  add_can_probe_invalid_cli_test(can_probe_duration_negative --interface unused0
                                 --duration-ms -1)
  set_tests_properties(can_probe_help PROPERTIES PASS_REGULAR_EXPRESSION
                                                "Usage: robot-control-can-probe")
  set_tests_properties(
    platform_probe_run PROPERTIES PASS_REGULAR_EXPRESSION
                                  "probe_version=1.*machine=")
endif()
