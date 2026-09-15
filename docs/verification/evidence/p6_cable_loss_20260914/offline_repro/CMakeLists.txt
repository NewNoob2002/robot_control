cmake_minimum_required(VERSION 3.22)
project(cable_failure_reproduction LANGUAGES C CXX)
set(CMAKE_CXX_EXTENSIONS OFF)
get_filename_component(REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(ROBOT_CONTROL_BUILD_ZLAC_QUALIFICATION ON CACHE BOOL "" FORCE)
add_subdirectory("${REPO_ROOT}" robot-control)
add_executable(cable_gate_repro
  "${REPO_ROOT}/tests/unit/canopen_qualification_vcan_tests.cpp"
  "${REPO_ROOT}/tests/unit/canopen_test_log_stub.c"
  trace_gate.c)
target_link_options(cable_gate_repro PRIVATE "-Wl,--wrap=robot_control_canopen_qualification_transmit")
target_link_libraries(cable_gate_repro PRIVATE robot_control_communication_canopen_qualification
  robot_control_platform_linux robot_control_project_options robot_control_project_warnings)
