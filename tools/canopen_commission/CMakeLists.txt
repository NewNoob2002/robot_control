add_executable(robot-control-canopen-commission main.cpp commissioning_log.c)
target_link_libraries(
  robot-control-canopen-commission
  PRIVATE robot_control_communication_canopen_commissioning
          robot_control_project_options robot_control_project_warnings)

if(BUILD_TESTING AND NOT CMAKE_CROSSCOMPILING)
  foreach(arguments IN ITEMS "" "--unknown" "--interface;vcan0"
                             "--interface;vcan0;--nmt;operational"
                             "--interface;vcan0;--upload;6040:00"
                             "--interface;vcan0;--nmt;stopped;--retry-once")
    string(MAKE_C_IDENTIFIER "${arguments}" suffix)
    add_test(
      NAME canopen_commission_invalid_${suffix}
      COMMAND ${CMAKE_COMMAND}
              "-DPROGRAM=$<TARGET_FILE:robot-control-canopen-commission>"
              "-DPROGRAM_ARGUMENTS=${arguments}" -DEXPECTED_EXIT_CODE=2 -P
              "${PROJECT_SOURCE_DIR}/cmake/assert_exit_code.cmake")
  endforeach()
endif()
