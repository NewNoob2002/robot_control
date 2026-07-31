add_library(robot_control_domain_drive cia402.cpp)
target_include_directories(robot_control_domain_drive PUBLIC "${PROJECT_SOURCE_DIR}")
target_link_libraries(
  robot_control_domain_drive
  PUBLIC robot_control_domain_command robot_control_project_options
  PRIVATE robot_control_project_warnings)

