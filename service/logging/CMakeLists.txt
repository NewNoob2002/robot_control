add_library(robot_control_service_logging logger.cpp)
target_include_directories(robot_control_service_logging PUBLIC "${PROJECT_SOURCE_DIR}")
target_link_libraries(
  robot_control_service_logging
  PUBLIC robot_control_project_options
  PRIVATE robot_control_project_warnings)
