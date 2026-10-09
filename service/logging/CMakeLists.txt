set(SPDLOG_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_BENCH OFF CACHE BOOL "" FORCE)
set(SPDLOG_SYSTEM_INCLUDES ON CACHE BOOL "" FORCE)
set(SPDLOG_INSTALL OFF CACHE BOOL "" FORCE)

add_subdirectory("${PROJECT_SOURCE_DIR}/components/spdlog" "${CMAKE_CURRENT_BINARY_DIR}/spdlog_build" EXCLUDE_FROM_ALL)

add_library(robot_control_service_logging logger.cpp stderr_sink.cpp)
target_include_directories(robot_control_service_logging PUBLIC "${PROJECT_SOURCE_DIR}")
target_link_libraries(
  robot_control_service_logging
  PUBLIC robot_control_project_options
  PRIVATE robot_control_project_warnings spdlog::spdlog)
