add_executable(robot_control_domain_tests domain_tests.cpp)
target_link_libraries(
  robot_control_domain_tests
  PRIVATE robot_control_domain_control robot_control_domain_safety
          robot_control_domain_drive robot_control_project_options
          robot_control_project_warnings)
add_test(NAME domain_behavior_lock COMMAND robot_control_domain_tests)

add_executable(robot_control_sbus_parser_tests sbus_parser_tests.cpp)
target_link_libraries(
  robot_control_sbus_parser_tests
  PRIVATE robot_control_input_sbus_protocol robot_control_project_options
          robot_control_project_warnings)
add_test(NAME sbus_parser_contract COMMAND robot_control_sbus_parser_tests)

add_executable(robot_control_canopen_stack_tests canopen_stack_tests.cpp
                                                canopen_test_log_stub.c)
target_link_libraries(
  robot_control_canopen_stack_tests
  PRIVATE robot_control_communication_canopen robot_control_project_options
          robot_control_project_warnings)
add_test(NAME canopen_stack_build COMMAND robot_control_canopen_stack_tests)

add_executable(robot_control_canopen_vcan_tests canopen_vcan_tests.cpp
                                              canopen_test_log_stub.c)
target_link_options(robot_control_canopen_vcan_tests PRIVATE "-Wl,--wrap=recv" "-Wl,--wrap=CO_epoll_wait")
target_link_libraries(
  robot_control_canopen_vcan_tests
  PRIVATE robot_control_communication_canopen robot_control_platform_linux
          robot_control_project_options robot_control_project_warnings)
add_test(
  NAME canopen_vcan_managed
  COMMAND bash ${PROJECT_SOURCE_DIR}/scripts/test/test_socketcan_vcan.sh
          $<TARGET_FILE:robot_control_canopen_vcan_tests>)
set_tests_properties(canopen_vcan_managed PROPERTIES SKIP_RETURN_CODE 77
                                                     TIMEOUT 10)

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

if(ROBOT_CONTROL_BUILD_CANOPEN_COMMISSIONING)
  add_executable(robot_control_canopen_commissioning_gate_tests
                 canopen_commissioning_gate_tests.cpp)
  target_link_libraries(
    robot_control_canopen_commissioning_gate_tests
    PRIVATE robot_control_canopen_commissioning_gate robot_control_project_options
            robot_control_project_warnings)
  add_test(NAME canopen_commissioning_gate
           COMMAND robot_control_canopen_commissioning_gate_tests)

  add_executable(robot_control_canopen_commissioning_vcan_tests
                 canopen_commissioning_vcan_tests.cpp canopen_test_log_stub.c)
  target_link_libraries(
    robot_control_canopen_commissioning_vcan_tests
    PRIVATE robot_control_communication_canopen_commissioning
            robot_control_platform_linux robot_control_project_options
            robot_control_project_warnings)
  add_test(
    NAME canopen_commissioning_vcan_managed
    COMMAND bash ${PROJECT_SOURCE_DIR}/scripts/test/test_socketcan_vcan.sh
            $<TARGET_FILE:robot_control_canopen_commissioning_vcan_tests>)
  set_tests_properties(canopen_commissioning_vcan_managed
                       PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 15)
endif()

if(ROBOT_CONTROL_BUILD_ZLAC_QUALIFICATION)
  add_executable(robot_control_can_interface_inhibitor_tests
                 can_interface_inhibitor_tests.cpp)
  target_link_libraries(
    robot_control_can_interface_inhibitor_tests
    PRIVATE robot_control_platform_linux robot_control_project_options
            robot_control_project_warnings)
  add_test(
    NAME can_interface_inhibitor_vcan_managed
    COMMAND bash ${PROJECT_SOURCE_DIR}/scripts/test/test_can_inhibitor_vcan.sh
            $<TARGET_FILE:robot_control_can_interface_inhibitor_tests>
            $<TARGET_FILE:robot-control-can-interface-inhibitor>
            $<TARGET_FILE:robot-control-zlac-qualification>)
  set_tests_properties(can_interface_inhibitor_vcan_managed
                       PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 10)

  add_executable(robot_control_canopen_qualification_gate_tests
                 canopen_qualification_gate_tests.cpp)
  target_link_libraries(
    robot_control_canopen_qualification_gate_tests
    PRIVATE robot_control_canopen_qualification_gate robot_control_project_options
            robot_control_project_warnings)
  add_test(NAME canopen_qualification_gate
           COMMAND robot_control_canopen_qualification_gate_tests)

  add_executable(robot_control_canopen_qualification_layout_vcan_tests
    canopen_qualification_layout_vcan_tests.cpp canopen_test_log_stub.c)
  target_link_libraries(robot_control_canopen_qualification_layout_vcan_tests PRIVATE
    robot_control_communication_canopen_qualification robot_control_platform_linux robot_control_project_warnings)
  add_test(NAME canopen_qualification_layout_vcan_managed
    COMMAND bash ${PROJECT_SOURCE_DIR}/scripts/test/test_socketcan_vcan.sh
      $<TARGET_FILE:robot_control_canopen_qualification_layout_vcan_tests>)
  set_tests_properties(canopen_qualification_layout_vcan_managed PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 20)
  add_executable(robot_control_canopen_qualification_vcan_tests
                 canopen_qualification_vcan_tests.cpp canopen_test_log_stub.c)
  target_link_libraries(
    robot_control_canopen_qualification_vcan_tests
    PRIVATE robot_control_communication_canopen_qualification
            robot_control_platform_linux robot_control_project_options
            robot_control_project_warnings)
  add_test(
    NAME canopen_qualification_vcan_managed
    COMMAND bash ${PROJECT_SOURCE_DIR}/scripts/test/test_socketcan_vcan.sh
            $<TARGET_FILE:robot_control_canopen_qualification_vcan_tests>)
  set_tests_properties(canopen_qualification_vcan_managed
                       PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 120)
endif()
