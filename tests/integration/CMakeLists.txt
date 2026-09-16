add_executable(robot_control_linux_platform_tests linux_platform_tests.cpp)
target_link_libraries(
  robot_control_linux_platform_tests
  PRIVATE robot_control_platform_linux robot_control_service_logging
          robot_control_project_options robot_control_project_warnings)
add_test(NAME linux_platform_integration COMMAND robot_control_linux_platform_tests)
target_link_options(robot_control_linux_platform_tests PRIVATE "-Wl,--wrap=write")

add_executable(robot_control_sbus_reader_tests sbus_reader_tests.cpp)
target_link_libraries(robot_control_sbus_reader_tests
  PRIVATE robot_control_input_sbus_linux robot_control_project_warnings)
target_link_options(robot_control_sbus_reader_tests PRIVATE "-Wl,--wrap=read")
add_test(NAME sbus_reader_pty COMMAND robot_control_sbus_reader_tests)
set_tests_properties(sbus_reader_pty PROPERTIES TIMEOUT 10)

find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME sbus_observer_cli
  COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/scripts/test/test_sbus_observer.py"
          $<TARGET_FILE:robot-control-sbus-observer>)
set_tests_properties(sbus_observer_cli PROPERTIES TIMEOUT 15)
