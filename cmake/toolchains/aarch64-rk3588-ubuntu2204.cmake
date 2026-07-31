set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

if(DEFINED ENV{ROBOT_CONTROL_SYSROOT} AND NOT "$ENV{ROBOT_CONTROL_SYSROOT}" STREQUAL "")
  file(REAL_PATH "$ENV{ROBOT_CONTROL_SYSROOT}" _robot_control_sysroot)
else()
  message(FATAL_ERROR
    "ROBOT_CONTROL_SYSROOT is required. Point it to a validated Ubuntu 22.04 "
    "aarch64 sysroot; the Ubuntu cross-container filesystem is not valid.")
endif()

if(NOT IS_DIRECTORY "${_robot_control_sysroot}/usr/include")
  message(FATAL_ERROR
    "Invalid sysroot '${_robot_control_sysroot}': usr/include is missing")
endif()

if(NOT EXISTS "${_robot_control_sysroot}/lib/ld-linux-aarch64.so.1"
   AND NOT EXISTS "${_robot_control_sysroot}/lib/aarch64-linux-gnu/ld-linux-aarch64.so.1")
  message(FATAL_ERROR
    "Invalid aarch64 sysroot '${_robot_control_sysroot}': dynamic loader missing")
endif()

set(CMAKE_SYSROOT "${_robot_control_sysroot}")
set(CMAKE_C_COMPILER aarch64-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER aarch64-linux-gnu-g++)

set(CMAKE_FIND_ROOT_PATH "${CMAKE_SYSROOT}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

set(ENV{PKG_CONFIG_SYSROOT_DIR} "${CMAKE_SYSROOT}")
set(ENV{PKG_CONFIG_LIBDIR}
  "${CMAKE_SYSROOT}/usr/lib/aarch64-linux-gnu/pkgconfig:${CMAKE_SYSROOT}/usr/lib/pkgconfig:${CMAKE_SYSROOT}/usr/share/pkgconfig")
set(ENV{PKG_CONFIG_PATH} "")

set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
