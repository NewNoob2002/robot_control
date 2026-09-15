add_executable(robot-control-can-interface-inhibitor main.cpp)
target_link_libraries(
  robot-control-can-interface-inhibitor
  PRIVATE robot_control_platform_linux robot_control_project_options
          robot_control_project_warnings)
