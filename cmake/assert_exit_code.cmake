if(NOT DEFINED PROGRAM OR NOT DEFINED EXPECTED_EXIT_CODE)
  message(FATAL_ERROR "PROGRAM and EXPECTED_EXIT_CODE are required")
endif()

execute_process(
  COMMAND "${PROGRAM}" ${PROGRAM_ARGUMENTS}
  RESULT_VARIABLE actual_exit_code)

string(JOIN " " formatted_arguments ${PROGRAM_ARGUMENTS})
if(NOT "${actual_exit_code}" MATCHES "^[0-9]+$" OR
   NOT "${actual_exit_code}" STREQUAL "${EXPECTED_EXIT_CODE}")
  message(
    FATAL_ERROR
      "program=${PROGRAM} arguments=[${formatted_arguments}] expected_exit_code=${EXPECTED_EXIT_CODE} actual_result=${actual_exit_code}")
endif()
