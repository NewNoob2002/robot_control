add_executable(robot-control-can-probe main.cpp)
target_link_libraries(
  robot-control-can-probe
  PRIVATE robot_control_platform_linux robot_control_project_options
          robot_control_project_warnings)

install(TARGETS robot-control-can-probe RUNTIME DESTINATION bin)
