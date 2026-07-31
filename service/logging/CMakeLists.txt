find_package(Threads REQUIRED)

add_library(
  robot_control_easylogger
  "${PROJECT_SOURCE_DIR}/components/EasyLogger/easylogger/src/elog.c"
  "${PROJECT_SOURCE_DIR}/components/EasyLogger/easylogger/src/elog_utils.c"
  easylogger_port.c)
target_include_directories(
  robot_control_easylogger
  PUBLIC "${PROJECT_SOURCE_DIR}/components/EasyLogger/easylogger/inc"
  PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/easylogger_config")
target_compile_options(
  robot_control_easylogger
  PRIVATE
    "-include${CMAKE_CURRENT_SOURCE_DIR}/easylogger_config/elog_cfg.h")
target_link_libraries(robot_control_easylogger PRIVATE Threads::Threads)

add_library(robot_control_service_logging logger.cpp)
target_include_directories(robot_control_service_logging PUBLIC "${PROJECT_SOURCE_DIR}")
target_link_libraries(
  robot_control_service_logging
  PUBLIC robot_control_project_options
  PRIVATE robot_control_project_warnings robot_control_easylogger)
