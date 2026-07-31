add_executable(robot-control-platform-probe main.cpp)
target_link_libraries(
  robot-control-platform-probe
  PRIVATE robot_control_project_options robot_control_project_warnings)

install(TARGETS robot-control-platform-probe RUNTIME DESTINATION bin)

