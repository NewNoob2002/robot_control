add_library(
  robot_control_platform_linux
  can/classic_frame.cpp
  can/socket.cpp
  io/poll_wait.cpp
  process/termination_event.cpp
  time/monotonic_timer.cpp
  uart/serial_port.cpp)
target_include_directories(robot_control_platform_linux PUBLIC "${PROJECT_SOURCE_DIR}")
target_link_libraries(
  robot_control_platform_linux
  PUBLIC robot_control_project_options
  PRIVATE robot_control_project_warnings)
