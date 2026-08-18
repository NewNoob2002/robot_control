add_executable(robot_control_domain_tests domain_tests.cpp)
target_link_libraries(
  robot_control_domain_tests
  PRIVATE robot_control_domain_control robot_control_domain_safety
          robot_control_domain_drive robot_control_project_options
          robot_control_project_warnings)
add_test(NAME domain_behavior_lock COMMAND robot_control_domain_tests)

add_executable(robot_control_socketcan_frame_tests socketcan_frame_tests.cpp)
target_link_libraries(
  robot_control_socketcan_frame_tests
  PRIVATE robot_control_platform_linux robot_control_project_options
          robot_control_project_warnings)
add_test(NAME socketcan_frame_codec COMMAND robot_control_socketcan_frame_tests)
