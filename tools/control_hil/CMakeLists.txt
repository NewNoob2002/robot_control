add_executable(robot-control-hil main.cpp zero_gate.cpp ../zlac_qualification/qualification_log.c)
target_link_libraries(robot-control-hil PRIVATE robot_control_application_loop
  robot_control_platform_linux robot_control_project_warnings)
target_link_options(robot-control-hil PRIVATE "-Wl,--wrap=send")

if(BUILD_TESTING AND NOT CMAKE_CROSSCOMPILING)
  add_executable(robot_control_hil_zero_gate_tests
    ${PROJECT_SOURCE_DIR}/tests/unit/control_hil_zero_gate_tests.cpp zero_gate.cpp
    $<TARGET_OBJECTS:robot_control_canopen_qualification_gate>)
  target_include_directories(robot_control_hil_zero_gate_tests PRIVATE ${PROJECT_SOURCE_DIR})
  target_link_libraries(robot_control_hil_zero_gate_tests PRIVATE robot_control_project_warnings)
  add_test(NAME control_hil_zero_gate COMMAND robot_control_hil_zero_gate_tests)
  add_test(NAME control_hil_help COMMAND robot-control-hil --help)
  add_test(NAME control_hil_zero_vcan_managed
    COMMAND bash ${PROJECT_SOURCE_DIR}/scripts/test/test_socketcan_vcan.sh
      ${PROJECT_SOURCE_DIR}/tests/integration/control_hil_zero_vcan.py)
  set_tests_properties(control_hil_zero_vcan_managed PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 40
    ENVIRONMENT "CONTROL_HIL_PROGRAM=$<TARGET_FILE:robot-control-hil>")

  add_executable(robot_control_control_qualification_vcan_tests
    ${PROJECT_SOURCE_DIR}/tests/unit/control_qualification_vcan_tests.cpp
    ${PROJECT_SOURCE_DIR}/tests/unit/canopen_test_log_stub.c)
  target_link_libraries(robot_control_control_qualification_vcan_tests PRIVATE
    robot_control_communication_canopen_runtime robot_control_platform_linux robot_control_project_warnings)
  add_test(NAME control_qualification_vcan_managed
    COMMAND bash ${PROJECT_SOURCE_DIR}/scripts/test/test_socketcan_vcan.sh
      $<TARGET_FILE:robot_control_control_qualification_vcan_tests>)
  set_tests_properties(control_qualification_vcan_managed PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 60)
endif()
