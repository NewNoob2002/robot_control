add_executable(robot-control-canopen-observer main.cpp observer_log.c)
target_link_libraries(
  robot-control-canopen-observer
  PRIVATE robot_control_communication_canopen robot_control_project_options
          robot_control_project_warnings)

install(TARGETS robot-control-canopen-observer RUNTIME DESTINATION bin)

if(BUILD_TESTING AND NOT CMAKE_CROSSCOMPILING)
  add_test(NAME canopen_observer_help
           COMMAND robot-control-canopen-observer --help)
  set_tests_properties(canopen_observer_help
                       PROPERTIES PASS_REGULAR_EXPRESSION "Usage:")
  foreach(arguments IN ITEMS "" "--unknown" "--interface;unused0"
                             "--interface;unused0;--interface;unused1"
                             "--duration-ms;0"
                             "--duration-ms;1;--duration-ms;2")
    string(MAKE_C_IDENTIFIER "${arguments}" suffix)
    add_test(
      NAME canopen_observer_invalid_${suffix}
      COMMAND ${CMAKE_COMMAND}
              "-DPROGRAM=$<TARGET_FILE:robot-control-canopen-observer>"
              "-DPROGRAM_ARGUMENTS=${arguments}" -DEXPECTED_EXIT_CODE=2 -P
              "${PROJECT_SOURCE_DIR}/cmake/assert_exit_code.cmake")
  endforeach()
endif()
