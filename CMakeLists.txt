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
  add_subdirectory(tests/unit)
  add_subdirectory(tests/integration)
  add_test(NAME platform_probe_help COMMAND robot-control-platform-probe --help)
  add_test(NAME platform_probe_run COMMAND robot-control-platform-probe)
  add_test(NAME platform_probe_phase3_smoke
           COMMAND robot-control-platform-probe --phase3-smoke)
  add_test(NAME can_probe_help COMMAND robot-control-can-probe --help)
  add_test(NAME can_probe_missing_interface COMMAND robot-control-can-probe)
  add_test(NAME can_probe_unknown_argument
           COMMAND robot-control-can-probe --unknown)
  add_test(NAME can_probe_duration_zero
           COMMAND robot-control-can-probe --interface unused0 --duration-ms 0)
  add_test(NAME can_probe_duration_nonnumeric
           COMMAND robot-control-can-probe --interface unused0 --duration-ms abc)
  add_test(
    NAME can_probe_duration_overflow
    COMMAND robot-control-can-probe --interface unused0 --duration-ms
            9999999999999999999999999999999999999999)
  set_tests_properties(
    can_probe_missing_interface can_probe_unknown_argument
    can_probe_duration_zero can_probe_duration_nonnumeric
    can_probe_duration_overflow PROPERTIES WILL_FAIL TRUE)
  set_tests_properties(can_probe_help PROPERTIES PASS_REGULAR_EXPRESSION
                                                "Usage: robot-control-can-probe")
  set_tests_properties(
    platform_probe_run PROPERTIES PASS_REGULAR_EXPRESSION
                                  "probe_version=1.*machine=")
endif()
