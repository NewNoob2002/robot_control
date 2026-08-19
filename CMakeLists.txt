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
add_subdirectory(platform)
add_subdirectory(service)
add_subdirectory(tools/can_probe)
add_subdirectory(tools/platform_probe)

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
