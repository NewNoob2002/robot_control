set(canopen_node_dir "${PROJECT_SOURCE_DIR}/components/CANopenLinux/CANopenNode")
set(canopen_linux_driver
    "${PROJECT_SOURCE_DIR}/components/CANopenLinux/CO_driver.c")

add_library(robot_control_canopen_transmit_gate OBJECT transmit_gate.c)
target_link_libraries(robot_control_canopen_transmit_gate
                      PRIVATE robot_control_project_warnings)

set(canopen_upstream_sources
    ${canopen_linux_driver}
    ${PROJECT_SOURCE_DIR}/components/CANopenLinux/CO_epoll_interface.c
    ${canopen_node_dir}/CANopen.c
    ${canopen_node_dir}/301/CO_ODinterface.c
    ${canopen_node_dir}/301/CO_NMT_Heartbeat.c
    ${canopen_node_dir}/301/CO_HBconsumer.c
    ${canopen_node_dir}/301/CO_Emergency.c
    ${canopen_node_dir}/301/CO_SDOserver.c
    ${canopen_node_dir}/301/CO_SDOclient.c
    ${canopen_node_dir}/301/CO_PDO.c
    ${canopen_node_dir}/301/CO_fifo.c
    od/OD.c)

add_library(
  robot_control_canopen_upstream STATIC
  ${canopen_upstream_sources}
  $<TARGET_OBJECTS:robot_control_canopen_transmit_gate>)
target_compile_definitions(
  robot_control_canopen_upstream
  PRIVATE send=robot_control_canopen_deny_transmit
  PUBLIC CO_DRIVER_CUSTOM CO_MULTIPLE_OD CO_SINGLE_THREAD)
target_include_directories(robot_control_canopen_upstream
                           PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(
  robot_control_canopen_upstream
  SYSTEM
  PUBLIC ${PROJECT_SOURCE_DIR}/components/CANopenLinux ${canopen_node_dir}
         ${CMAKE_CURRENT_SOURCE_DIR}/od)

add_library(robot_control_communication_canopen lifecycle.cpp observation.cpp
                                                stack_config.cpp stack_storage.cpp)
target_include_directories(robot_control_communication_canopen
                           PUBLIC ${PROJECT_SOURCE_DIR})
target_link_libraries(
  robot_control_communication_canopen
  PUBLIC robot_control_canopen_upstream
  PRIVATE robot_control_platform_linux robot_control_project_options
          robot_control_project_warnings)

if(ROBOT_CONTROL_BUILD_CANOPEN_COMMISSIONING)
  add_library(robot_control_canopen_commissioning_gate OBJECT
              commissioning_gate.c)
  target_include_directories(robot_control_canopen_commissioning_gate
                             PUBLIC ${PROJECT_SOURCE_DIR})
  target_link_libraries(robot_control_canopen_commissioning_gate
                        PRIVATE robot_control_project_warnings)

  add_library(
    robot_control_canopen_upstream_commissioning STATIC
    ${canopen_upstream_sources}
    $<TARGET_OBJECTS:robot_control_canopen_commissioning_gate>)
  target_compile_definitions(
    robot_control_canopen_upstream_commissioning
    PRIVATE send=robot_control_canopen_commissioning_transmit
    PUBLIC CO_DRIVER_CUSTOM CO_MULTIPLE_OD CO_SINGLE_THREAD
           ROBOT_CONTROL_CANOPEN_COMMISSIONING)
  target_include_directories(robot_control_canopen_upstream_commissioning
                             PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
  target_include_directories(
    robot_control_canopen_upstream_commissioning
    SYSTEM
    PUBLIC ${PROJECT_SOURCE_DIR}/components/CANopenLinux ${canopen_node_dir}
           ${CMAKE_CURRENT_SOURCE_DIR}/od)

  add_library(robot_control_communication_canopen_commissioning
              lifecycle.cpp observation.cpp stack_config.cpp stack_storage.cpp
              commissioning.cpp)
  target_include_directories(robot_control_communication_canopen_commissioning
                             PUBLIC ${PROJECT_SOURCE_DIR})
  target_link_libraries(
    robot_control_communication_canopen_commissioning
    PUBLIC robot_control_canopen_upstream_commissioning
    PRIVATE robot_control_platform_linux robot_control_project_options
            robot_control_project_warnings)
endif()
