set(canopen_node_dir "${PROJECT_SOURCE_DIR}/components/CANopenLinux/CANopenNode")
set(canopen_linux_driver
    "${PROJECT_SOURCE_DIR}/components/CANopenLinux/CO_driver.c")

add_library(robot_control_canopen_transmit_gate OBJECT transmit_gate.c)
target_link_libraries(robot_control_canopen_transmit_gate
                      PRIVATE robot_control_project_warnings)
set_source_files_properties(
  "${canopen_linux_driver}"
  PROPERTIES COMPILE_DEFINITIONS
             "send=robot_control_canopen_deny_transmit")

add_library(
  robot_control_canopen_upstream STATIC
  ${canopen_linux_driver}
  ${canopen_node_dir}/CANopen.c
  ${canopen_node_dir}/301/CO_ODinterface.c
  ${canopen_node_dir}/301/CO_NMT_Heartbeat.c
  ${canopen_node_dir}/301/CO_HBconsumer.c
  ${canopen_node_dir}/301/CO_Emergency.c
  ${canopen_node_dir}/301/CO_SDOserver.c
  ${canopen_node_dir}/301/CO_SDOclient.c
  ${canopen_node_dir}/301/CO_PDO.c
  ${canopen_node_dir}/301/CO_fifo.c
  od/OD.c
  $<TARGET_OBJECTS:robot_control_canopen_transmit_gate>)
target_compile_definitions(
  robot_control_canopen_upstream
  PUBLIC CO_DRIVER_CUSTOM CO_MULTIPLE_OD CO_SINGLE_THREAD)
target_include_directories(robot_control_canopen_upstream
                           PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(
  robot_control_canopen_upstream
  SYSTEM
  PUBLIC ${PROJECT_SOURCE_DIR}/components/CANopenLinux ${canopen_node_dir}
         ${CMAKE_CURRENT_SOURCE_DIR}/od)

add_library(robot_control_communication_canopen stack_config.cpp
                                                stack_storage.cpp)
target_include_directories(robot_control_communication_canopen
                           PUBLIC ${PROJECT_SOURCE_DIR})
target_link_libraries(
  robot_control_communication_canopen
  PUBLIC robot_control_canopen_upstream
  PRIVATE robot_control_project_options robot_control_project_warnings)
