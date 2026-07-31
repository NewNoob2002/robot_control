add_library(robot_control_domain_safety safety_manager.cpp)
target_include_directories(robot_control_domain_safety PUBLIC "${PROJECT_SOURCE_DIR}")
target_link_libraries(
  robot_control_domain_safety
  PUBLIC robot_control_domain_control robot_control_domain_drive
         robot_control_project_options
  PRIVATE robot_control_project_warnings)

