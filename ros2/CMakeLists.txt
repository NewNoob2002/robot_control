cmake_minimum_required(VERSION 3.22)

# Colcon builds only this adapter; the root CMake remains the middleware authority.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(robot_control_ros2 VERSION 0.1.0 LANGUAGES NONE)
  find_package(ament_cmake REQUIRED)
endif()

install(PROGRAMS robot_control_ipc_node.py
  DESTINATION lib/robot_control_ros2 RENAME robot_control_ipc_node COMPONENT ros2)
install(PROGRAMS robot_control_ipc_monitor.py
  DESTINATION lib/robot_control_ros2 RENAME robot_control_ipc_monitor COMPONENT ros2)
install(DIRECTORY launch DESTINATION share/robot_control_ros2 COMPONENT ros2)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  ament_package()
else()
  # Native/cross application installs require no ROS SDK or target Python imports.
  install(FILES package.xml DESTINATION share/robot_control_ros2 COMPONENT ros2)
  install(FILES resource/robot_control_ros2
    DESTINATION share/ament_index/resource_index/packages COMPONENT ros2)
  if(BUILD_TESTING AND NOT CMAKE_CROSSCOMPILING)
    add_test(NAME ros2_receipt_contract COMMAND python3
      ${PROJECT_SOURCE_DIR}/scripts/test/test_ros2_receipt.py)
  endif()
endif()
