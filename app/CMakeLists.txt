add_library(robot_control_startup_config ../application/config/startup.cpp)
target_link_libraries(robot_control_startup_config PUBLIC
  robot_control_application_control robot_control_input_sbus_source
  PRIVATE robot_control_project_warnings)
add_executable(robot-control-config-check config_check.cpp)
target_link_libraries(robot-control-config-check PRIVATE
  robot_control_startup_config robot_control_project_warnings)

if(BUILD_TESTING AND NOT CMAKE_CROSSCOMPILING)
  add_executable(robot_control_startup_config_tests ../tests/unit/startup_config_tests.cpp)
  target_link_libraries(robot_control_startup_config_tests PRIVATE
    robot_control_startup_config robot_control_project_warnings)
  add_test(NAME startup_stack_config_contract COMMAND robot_control_startup_config_tests)
  add_test(NAME startup_config_contract COMMAND python3
    ${PROJECT_SOURCE_DIR}/scripts/test/test_startup_config.py
    $<TARGET_FILE:robot-control-config-check> ${PROJECT_SOURCE_DIR}/config/rk3588-test.conf)
endif()

if(ROBOT_CONTROL_BUILD_TEST_APP)
  add_library(robot_control_app_ipc ../application/ipc/server.cpp)
  target_link_libraries(robot_control_app_ipc PUBLIC robot_control_platform_linux
    PRIVATE robot_control_project_warnings)
  add_executable(robot-control-device-config device_config.cpp ../tools/zlac_qualification/qualification_log.c)
  target_link_libraries(robot-control-device-config PRIVATE robot_control_startup_config
    robot_control_communication_canopen_runtime robot_control_platform_linux robot_control_project_warnings)
  target_link_options(robot-control-device-config PRIVATE "-Wl,--wrap=send")
  install(TARGETS robot-control-device-config RUNTIME DESTINATION .)
  add_executable(robot-control-app main.cpp io_gate.cpp runtime_log.cpp ../tools/zlac_qualification/qualification_log.c)
  target_link_libraries(robot-control-app PRIVATE robot_control_startup_config robot_control_application_loop
    robot_control_platform_linux robot_control_service_logging robot_control_app_ipc robot_control_project_warnings)
  target_link_options(robot-control-app PRIVATE "-Wl,--wrap=send" "-Wl,--wrap=recv")
  install(TARGETS robot-control-app RUNTIME DESTINATION .)
  install(PROGRAMS run-test.sh DESTINATION .)
  install(PROGRAMS ../ros2/robot_control_ipc_node.py DESTINATION .)
  install(FILES ${PROJECT_SOURCE_DIR}/config/rk3588-test.conf
    ${PROJECT_SOURCE_DIR}/docs/development/TEST_APP.md
    ${PROJECT_SOURCE_DIR}/docs/development/ROS2_IPC.md
    ${PROJECT_SOURCE_DIR}/docs/development/ROS2_PORTING_GUIDE.md DESTINATION .)
  install(FILES ${PROJECT_SOURCE_DIR}/components/CANopenLinux/LICENSE DESTINATION licenses RENAME CANopenLinux-LICENSE)
  install(FILES ${PROJECT_SOURCE_DIR}/components/CANopenLinux/CANopenNode/LICENSE DESTINATION licenses RENAME CANopenNode-LICENSE)
  install(FILES ${PROJECT_SOURCE_DIR}/components/spdlog/LICENSE DESTINATION licenses RENAME spdlog-LICENSE)
  # Device-free endpoint fixture is cross-built for target IPC/DDS checks as well.
  if(BUILD_TESTING)
    add_executable(robot_control_ipc_fixture ../tests/integration/ipc_fixture.cpp)
    target_link_libraries(robot_control_ipc_fixture PRIVATE robot_control_app_ipc robot_control_project_warnings)
  endif()
  if(BUILD_TESTING AND NOT CMAKE_CROSSCOMPILING)
    add_test(NAME app_ipc_contract COMMAND python3 ${PROJECT_SOURCE_DIR}/scripts/test/test_app_ipc.py
      $<TARGET_FILE:robot_control_ipc_fixture>)
    set_tests_properties(app_ipc_contract PROPERTIES TIMEOUT 20)
    add_executable(robot_control_device_parameter_gate_tests ../tests/unit/device_parameter_gate_tests.cpp)
    target_link_libraries(robot_control_device_parameter_gate_tests PRIVATE
      robot_control_communication_canopen_runtime robot_control_project_warnings)
    add_test(NAME device_parameter_gates COMMAND robot_control_device_parameter_gate_tests)
    add_test(NAME device_config_vcan_managed COMMAND bash ${PROJECT_SOURCE_DIR}/scripts/test/test_socketcan_vcan.sh
      ${PROJECT_SOURCE_DIR}/tests/integration/test_device_config_vcan.py)
    set_tests_properties(device_config_vcan_managed PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 90
      ENVIRONMENT "DEVICE_CONFIG_PROGRAM=$<TARGET_FILE:robot-control-device-config>;TEST_APP_CONFIG=${PROJECT_SOURCE_DIR}/config/rk3588-test.conf")
    add_executable(robot_control_app_motion_monitor_tests ../tests/unit/app_motion_monitor_tests.cpp)
    target_include_directories(robot_control_app_motion_monitor_tests PRIVATE ${PROJECT_SOURCE_DIR})
    target_link_libraries(robot_control_app_motion_monitor_tests PRIVATE robot_control_project_warnings)
    add_test(NAME test_app_motion_monitor COMMAND robot_control_app_motion_monitor_tests)
    add_test(NAME test_app_cli COMMAND python3 ${PROJECT_SOURCE_DIR}/scripts/test/test_app_cli.py
      $<TARGET_FILE:robot-control-app> ${PROJECT_SOURCE_DIR}/config/rk3588-test.conf)
    add_test(NAME test_app_vcan_managed COMMAND bash ${PROJECT_SOURCE_DIR}/scripts/test/test_socketcan_vcan.sh
      ${PROJECT_SOURCE_DIR}/tests/integration/test_app_vcan.py)
    set_tests_properties(test_app_vcan_managed PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 180
      ENVIRONMENT "TEST_APP_PROGRAM=$<TARGET_FILE:robot-control-app>;TEST_APP_CONFIG=${PROJECT_SOURCE_DIR}/config/rk3588-test.conf")
    # Both peers use the same cooperative /tmp interface lock despite separate network namespaces.
    set_tests_properties(device_config_vcan_managed test_app_vcan_managed
      PROPERTIES RESOURCE_LOCK robot_control_app_interface)
  endif()
endif()
