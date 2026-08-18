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

add_executable(robot_control_socketcan_socket_tests socketcan_socket_tests.cpp)
target_link_libraries(
  robot_control_socketcan_socket_tests
  PRIVATE robot_control_platform_linux robot_control_project_options
          robot_control_project_warnings)
add_test(NAME socketcan_socket_lifecycle
         COMMAND robot_control_socketcan_socket_tests)
add_test(
  NAME socketcan_vcan_managed
  COMMAND bash ${PROJECT_SOURCE_DIR}/scripts/test/test_socketcan_vcan.sh
          $<TARGET_FILE:robot_control_socketcan_socket_tests>)
set_tests_properties(socketcan_vcan_managed PROPERTIES SKIP_RETURN_CODE 77)
