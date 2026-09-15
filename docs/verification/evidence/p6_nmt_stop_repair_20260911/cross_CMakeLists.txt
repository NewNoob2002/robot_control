cmake_minimum_required(VERSION 3.22)
project(review_rk3588_executor LANGUAGES C CXX)
set(CMAKE_CXX_EXTENSIONS OFF)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(ROBOT_CONTROL_BUILD_ZLAC_QUALIFICATION ON CACHE BOOL "" FORCE)
add_subdirectory(/workspace robot-control)
add_executable(review_qualification_vcan_tests
  /workspace/tests/unit/canopen_qualification_vcan_tests.cpp
  /workspace/tests/unit/canopen_test_log_stub.c)
target_link_libraries(review_qualification_vcan_tests PRIVATE
  robot_control_communication_canopen_qualification robot_control_platform_linux
  robot_control_project_options robot_control_project_warnings)

add_executable(review_lifecycle_vcan_tests
 /workspace/tests/unit/canopen_vcan_tests.cpp
 /workspace/tests/unit/canopen_test_log_stub.c)
target_link_options(review_lifecycle_vcan_tests PRIVATE "-Wl,--wrap=recv" "-Wl,--wrap=CO_epoll_wait")
target_link_libraries(review_lifecycle_vcan_tests PRIVATE
 robot_control_communication_canopen robot_control_platform_linux
 robot_control_project_options robot_control_project_warnings)
