add_executable(robot-control-sbus-observer main.cpp)
target_link_libraries(robot-control-sbus-observer
  PRIVATE robot_control_input_sbus_linux robot_control_project_warnings)
install(TARGETS robot-control-sbus-observer RUNTIME DESTINATION bin)
