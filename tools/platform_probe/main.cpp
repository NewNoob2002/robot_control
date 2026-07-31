#include "platform/linux/process/termination_event.hpp"
#include "platform/linux/time/monotonic_timer.hpp"
#include "platform/linux/uart/serial_port.hpp"
#include "service/logging/logger.hpp"

#include <sys/utsname.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstring>
#include <iostream>
#include <string_view>

namespace {

/**
 * Print command-line usage for the platform probe.
 *
 * Thread safety: This function writes to the process standard output and must
 * not be called concurrently with other unsynchronized writers.
 */
void print_usage() {
  std::cout << "Usage: robot-control-platform-probe [--help|--phase3-smoke]\n";
}

/**
 * Link and safely execute representative Phase 3 APIs.
 *
 * @return Zero when mechanisms initialize and expected failure paths work.
 */
int phase3_smoke() {
  namespace linux_time = robot_control::platform::linux::time;
  const auto current = linux_time::now();
  if (!current.ok() || !linux_time::sleep_until(current.value()).ok()) {
    return 3;
  }
  auto termination =
      robot_control::platform::linux::process::TerminationEvent::create();
  if (!termination.ok()) {
    return 4;
  }
  const auto missing = robot_control::platform::linux::uart::SerialPort::open(
      "/definitely/missing/robot-control-phase3-probe", {});
  if (missing.ok()) {
    return 5;
  }
  robot_control::service::logging::Logger logger;
  logger.log(robot_control::service::logging::Severity::info, "platform_probe",
             "phase3_smoke", "result=pass");
  return 0;
}

/**
 * Report target runtime properties needed by the Phase 1 ABI smoke test.
 *
 * The probe performs no device access and never changes target state.
 *
 * @return Zero on success; a non-zero value when the kernel identity cannot be
 * read.
 *
 * Thread safety: This function reads process and kernel state only. Standard
 * output writes require external synchronization when called concurrently.
 */
int report_platform() {
  utsname identity{};
  if (::uname(&identity) != 0) {
    std::cerr << "operation=uname errno=" << errno << " message=\""
              << std::strerror(errno) << "\"\n";
    return 2;
  }

  const auto monotonic_now =
      std::chrono::steady_clock::now().time_since_epoch();
  const auto monotonic_ns =
      std::chrono::duration_cast<std::chrono::nanoseconds>(monotonic_now)
          .count();

  std::cout << "probe_version=1\n"
            << "sysname=" << identity.sysname << '\n'
            << "release=" << identity.release << '\n'
            << "machine=" << identity.machine << '\n'
            << "pid=" << ::getpid() << '\n'
            << "monotonic_ns=" << monotonic_ns << '\n';
  return 0;
}

} // namespace

/**
 * Run the Phase 1 platform/ABI probe.
 *
 * @param argc Number of command-line arguments.
 * @param argv Null-terminated argument vector owned by the process runtime.
 * @return Zero on success, one for invalid arguments, or a probe error code.
 */
int main(const int argc, char *argv[]) {
  if (argc == 2 && std::string_view{argv[1]} == "--help") {
    print_usage();
    return 0;
  }
  if (argc == 2 && std::string_view{argv[1]} == "--phase3-smoke") {
    return phase3_smoke();
  }
  if (argc != 1) {
    print_usage();
    return 1;
  }
  return report_platform();
}
