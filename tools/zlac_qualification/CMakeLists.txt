add_executable(robot-control-zlac-qualification main.cpp qualification_log.c)
target_link_libraries(
  robot-control-zlac-qualification
  PRIVATE robot_control_communication_canopen_qualification
          robot_control_project_options robot_control_project_warnings)

if(BUILD_TESTING AND NOT CMAKE_CROSSCOMPILING)
  foreach(arguments IN ITEMS "--interface;vcan0;--manual-tpdo;--duration-ms;1000"
                             "--interface;vcan0;--manual-tpdo;--watchdog-once"
                             "" "--unknown" "--interface;vcan0"
                             "--interface;vcan0;--nmt;reset-node"
                             "--interface;vcan0;--controlword;quick-stop"
                             "--interface;vcan0;--target-once;1:11;--duration-ms;10"
                             "--interface;vcan0;--target-once;1:5;--duration-ms;3001"
                             "--interface;vcan0;--target-once;1:1"
                             "--interface;vcan0;--nmt-stop-once;2:5;--duration-ms;2001"
                             "--interface;vcan0;--nmt-stop-once;2:5"
                             "--interface;vcan0;--shutdown-once;2:5"
                             "--interface;vcan0;--shutdown-once;2:5;--duration-ms;10001"
                             "--interface;vcan0;--disable-voltage-once;2:5"
                             "--interface;vcan0;--disable-voltage-once;2:5;--duration-ms;10001"
                             "--interface;vcan0;--quick-stop-once;2:5"
                             "--interface;vcan0;--quick-stop-once;2:5;--duration-ms;10001"
                             "--interface;vcan0;--watchdog-once;--duration-ms;1500"
                             "--interface;vcan0;--heartbeat-loss-once;2:5"
                             "--interface;vcan0;--tpdo-loss-once;--watchdog-once"
                             "--interface;vcan0;--zero-sequence;--zero-targets"
                             "--interface;vcan0;--zero-sequence;--duration-ms;10")
    string(MAKE_C_IDENTIFIER "${arguments}" suffix)
    add_test(
      NAME zlac_qualification_invalid_${suffix}
      COMMAND ${CMAKE_COMMAND}
              "-DPROGRAM=$<TARGET_FILE:robot-control-zlac-qualification>"
              "-DPROGRAM_ARGUMENTS=${arguments}" -DEXPECTED_EXIT_CODE=2 -P
              "${PROJECT_SOURCE_DIR}/cmake/assert_exit_code.cmake")
  endforeach()
endif()
