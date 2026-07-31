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
  std::cout << "Usage: robot-control-platform-probe [--help]\n";
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
  if (argc != 1) {
    print_usage();
    return 1;
  }
  return report_platform();
}
