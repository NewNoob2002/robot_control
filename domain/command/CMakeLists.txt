add_library(robot_control_domain_command INTERFACE)
target_include_directories(robot_control_domain_command
                           INTERFACE "${PROJECT_SOURCE_DIR}")
target_link_libraries(robot_control_domain_command
                      INTERFACE robot_control_project_options)

