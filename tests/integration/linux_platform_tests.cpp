#include "platform/linux/io/poll_wait.hpp"
#include "platform/linux/process/termination_event.hpp"
#include "platform/linux/time/monotonic_timer.hpp"
#include "platform/linux/uart/serial_port.hpp"
#include "platform/linux/unique_fd.hpp"
#include "service/logging/logger.hpp"

#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <string_view>
#include <thread>

namespace {

using namespace std::chrono_literals;
using robot_control::platform::linux::UniqueFd;
namespace io = robot_control::platform::linux::io;
namespace process = robot_control::platform::linux::process;
namespace platform_time = robot_control::platform::linux::time;
namespace uart = robot_control::platform::linux::uart;
namespace logging = robot_control::service::logging;

int failures = 0;

/** No-op handler used to force an interruptible wait to return EINTR. */
extern "C" void handle_test_signal(int) {}

/** Record one integration assertion failure. */
void check(const bool condition, const std::string_view id,
           const std::string_view expression) {
  if (!condition) {
    ++failures;
    std::cerr << "FAIL id=" << id << " expression=\"" << expression << "\"\n";
  }
}

#define CHECK(id, expression) check((expression), (id), #expression)

/** Verify move-only descriptor ownership and poll readiness/timeout behavior.
 */
void test_fd_and_poll() {
  std::array<int, 2> pipe_fds{};
  CHECK("FD-001", ::pipe2(pipe_fds.data(), O_CLOEXEC | O_NONBLOCK) == 0);
  UniqueFd reader{pipe_fds[0]};
  UniqueFd writer{pipe_fds[1]};
  UniqueFd moved{std::move(reader)};
  CHECK("FD-002", static_cast<bool>(moved));

  const int duplicate = ::dup(writer.get());
  CHECK("FD-003", duplicate >= 0);
  {
    UniqueFd scoped{duplicate};
    CHECK("FD-003", ::fcntl(scoped.get(), F_GETFD) >= 0);
  }
  errno = 0;
  CHECK("FD-003", ::fcntl(duplicate, F_GETFD) == -1 && errno == EBADF);

  auto event = io::wait_readable(moved.get(), 1ms);
  CHECK("POLL-001", event.ok());
  if (!event.ok()) {
    return;
  }
  CHECK("POLL-001", !event.value().readable);

  const std::byte value{0x5a};
  CHECK("POLL-002", ::write(writer.get(), &value, sizeof(value)) == 1);
  event = io::wait_readable(moved.get(), 50ms);
  CHECK("POLL-002", event.ok());
  if (!event.ok()) {
    return;
  }
  CHECK("POLL-002", event.value().readable);

  std::array<int, 2> cancel_fds{};
  CHECK("POLL-003", ::pipe2(cancel_fds.data(), O_CLOEXEC | O_NONBLOCK) == 0);
  UniqueFd cancel_reader{cancel_fds[0]};
  UniqueFd cancel_writer{cancel_fds[1]};
  CHECK("POLL-003", ::write(cancel_writer.get(), &value, sizeof(value)) == 1);
  event = io::wait_readable(moved.get(), 5s, cancel_reader.get());
  CHECK("POLL-003", event.ok());
  if (!event.ok()) {
    return;
  }
  CHECK("POLL-003", event.value().cancelled);

  const auto bad = io::wait_readable(-1, 1ms);
  CHECK("POLL-004", !bad.ok());
  CHECK("POLL-004", bad.status().operation == "poll");

  std::array<int, 2> interrupt_fds{};
  CHECK("POLL-005", ::pipe2(interrupt_fds.data(), O_CLOEXEC | O_NONBLOCK) == 0);
  UniqueFd interrupt_reader{interrupt_fds[0]};
  UniqueFd interrupt_writer{interrupt_fds[1]};
  CHECK("POLL-005", static_cast<bool>(interrupt_writer));
  struct sigaction action{};
  action.sa_handler = handle_test_signal;
  CHECK("POLL-005", ::sigemptyset(&action.sa_mask) == 0);
  struct sigaction old_action{};
  CHECK("POLL-005", ::sigaction(SIGUSR1, &action, &old_action) == 0);
  const pthread_t waiting_thread = ::pthread_self();
  std::thread interrupter{[waiting_thread] {
    for (int index = 0; index < 4; ++index) {
      std::this_thread::sleep_for(2ms);
      static_cast<void>(::pthread_kill(waiting_thread, SIGUSR1));
    }
  }};
  const auto wait_start = std::chrono::steady_clock::now();
  const auto interrupted = io::wait_readable(interrupt_reader.get(), 20ms);
  const auto wait_elapsed = std::chrono::steady_clock::now() - wait_start;
  interrupter.join();
  CHECK("POLL-005", ::sigaction(SIGUSR1, &old_action, nullptr) == 0);
  CHECK("POLL-005", interrupted.ok());
  CHECK("POLL-005", wait_elapsed >= 10ms && wait_elapsed < 100ms);
}

/** Verify absolute monotonic sleeping and invalid-period error propagation. */
void test_monotonic_timer() {
  const auto start = platform_time::now();
  CHECK("TIME-001", start.ok());
  if (!start.ok()) {
    return;
  }
  const auto status = platform_time::sleep_until(start.value() + 2ms);
  CHECK("TIME-002", status.ok());
  const auto wake = platform_time::now();
  CHECK("TIME-002", wake.ok());
  if (!wake.ok()) {
    return;
  }
  CHECK("TIME-002", wake.value() >= start.value() + 2ms);

  platform_time::PeriodicDeadline periodic{wake.value() + 1ms, 1ms};
  const auto periodic_result = periodic.wait_next();
  CHECK("TIME-003", periodic_result.ok());
  CHECK("TIME-003", periodic.next() > wake.value() + 1ms);

  platform_time::PeriodicDeadline overdue{wake.value() - 2s, 1ns};
  const auto overdue_start = std::chrono::steady_clock::now();
  const auto overdue_result = overdue.wait_next();
  CHECK("TIME-004", overdue_result.ok());
  CHECK("TIME-004", std::chrono::steady_clock::now() - overdue_start < 100ms);
  CHECK("TIME-004", overdue_result.ok() && overdue_result.value() > 1'000'000);

  std::array<int, 2> cancel_fds{};
  CHECK("TIME-005", ::pipe2(cancel_fds.data(), O_CLOEXEC | O_NONBLOCK) == 0);
  UniqueFd cancel_reader{cancel_fds[0]};
  UniqueFd cancel_writer{cancel_fds[1]};
  const std::byte value{0x1};
  CHECK("TIME-005", ::write(cancel_writer.get(), &value, sizeof(value)) == 1);
  const auto cancelled =
      platform_time::sleep_until(wake.value() + 5s, cancel_reader.get());
  CHECK("TIME-005", !cancelled.ok() && cancelled.error.value() == ECANCELED);

  platform_time::PeriodicDeadline invalid{start.value(), 0ns};
  const auto result = invalid.wait_next();
  CHECK("TIME-006", !result.ok());
  CHECK("TIME-006", result.status().operation == "periodic_wait");
}

/** Verify SIGTERM is consumed synchronously through signalfd. */
void test_termination_event() {
  auto created = process::TerminationEvent::create();
  CHECK("SIGNAL-001", created.ok());
  if (!created.ok()) {
    return;
  }
  {
    auto event = std::move(created).value();
    CHECK("SIGNAL-002", ::kill(::getpid(), SIGTERM) == 0);
    const auto ready = io::wait_readable(event.fd(), 100ms);
    CHECK("SIGNAL-002", ready.ok());
    if (!ready.ok()) {
      return;
    }
    CHECK("SIGNAL-002", ready.value().readable);
    const auto signal = event.consume();
    CHECK("SIGNAL-003", signal.ok());
    if (!signal.ok()) {
      return;
    }
    CHECK("SIGNAL-003", signal.value() == SIGTERM);
    const auto empty = event.consume();
    CHECK("SIGNAL-004", empty.ok());
    if (!empty.ok()) {
      return;
    }
    CHECK("SIGNAL-004", empty.value() == 0);
  }
  sigset_t current_mask{};
  CHECK("SIGNAL-005",
        ::pthread_sigmask(SIG_BLOCK, nullptr, &current_mask) == 0);
  CHECK("SIGNAL-005", ::sigismember(&current_mask, SIGTERM) == 1);
}

/** Verify raw tty configuration, fragmented reads, timeout, and disconnect. */
void test_serial_port() {
  const int master_descriptor =
      ::posix_openpt(O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
  CHECK("UART-001", master_descriptor >= 0);
  if (master_descriptor < 0) {
    return;
  }
  UniqueFd master{master_descriptor};
  CHECK("UART-001", ::grantpt(master.get()) == 0);
  CHECK("UART-001", ::unlockpt(master.get()) == 0);
  std::array<char, 256> slave_path{};
  CHECK("UART-001",
        ::ptsname_r(master.get(), slave_path.data(), slave_path.size()) == 0);

  auto opened = uart::SerialPort::open(slave_path.data(), uart::SerialConfig{});
  CHECK("UART-002", opened.ok());
  if (!opened.ok()) {
    return;
  }
  auto serial = std::move(opened).value();
  termios attributes{};
  CHECK("UART-003", ::tcgetattr(serial.fd(), &attributes) == 0);
  CHECK("UART-003", ::cfgetispeed(&attributes) == B115200);
  CHECK("UART-003", ::cfgetospeed(&attributes) == B115200);

  std::array<std::byte, 8> buffer{};
  auto count = serial.read_some(buffer, 1ms);
  CHECK("UART-004", count.ok());
  if (!count.ok()) {
    return;
  }
  CHECK("UART-004", count.value() == 0);

  const std::array<std::byte, 2> first{std::byte{0x11}, std::byte{0x22}};
  const std::array<std::byte, 1> second{std::byte{0x33}};
  CHECK("UART-005", ::write(master.get(), first.data(), first.size()) ==
                        static_cast<ssize_t>(first.size()));
  count = serial.read_some(buffer, 100ms);
  CHECK("UART-005", count.ok());
  if (!count.ok()) {
    return;
  }
  CHECK("UART-005", count.value() == first.size());
  CHECK("UART-005",
        std::memcmp(buffer.data(), first.data(), first.size()) == 0);
  CHECK("UART-006", ::write(master.get(), second.data(), second.size()) ==
                        static_cast<ssize_t>(second.size()));
  count = serial.read_some(buffer, 100ms);
  CHECK("UART-006", count.ok() && count.value() == second.size());

  std::array<int, 2> cancel_fds{};
  CHECK("UART-007", ::pipe2(cancel_fds.data(), O_CLOEXEC | O_NONBLOCK) == 0);
  UniqueFd cancel_reader{cancel_fds[0]};
  UniqueFd cancel_writer{cancel_fds[1]};
  const std::byte cancel_byte{0x1};
  CHECK("UART-007",
        ::write(cancel_writer.get(), &cancel_byte, sizeof(cancel_byte)) == 1);
  count = serial.read_some(buffer, 5s, cancel_reader.get());
  CHECK("UART-007", !count.ok() && count.status().error.value() == ECANCELED);

  master.reset();
  count = serial.read_some(buffer, 100ms);
  CHECK("UART-008", !count.ok());
  CHECK("UART-008", count.status().error.value() == EIO);
  CHECK("UART-008",
        count.status().context.find(slave_path.data()) != std::string::npos);

  const auto missing =
      uart::SerialPort::open("/definitely/missing/robot-control-tty", {});
  CHECK("UART-009", !missing.ok());
  CHECK("UART-009", missing.status().operation == "open");
}

/** Verify structured fields and deterministic repetition throttling. */
void test_logger() {
  std::array<int, 2> capture_fds{};
  CHECK("LOG-001", ::pipe2(capture_fds.data(), O_CLOEXEC) == 0);
  UniqueFd capture_reader{capture_fds[0]};
  UniqueFd capture_writer{capture_fds[1]};
  const int saved_stderr = ::dup(STDERR_FILENO);
  CHECK("LOG-001", saved_stderr >= 0);
  UniqueFd restore_stderr{saved_stderr};
  CHECK("LOG-001", ::dup2(capture_writer.get(), STDERR_FILENO) >= 0);
  logging::Logger logger;
  const std::chrono::steady_clock::time_point t0{};
  logger.log(logging::Severity::info, "test", "started", "value=1");
  logger.log_throttled("repeat", 100ms, logging::Severity::warning, "test",
                       "repeat", "code=2", t0);
  logger.log_throttled("repeat", 100ms, logging::Severity::warning, "test",
                       "repeat", "code=2", t0 + 1ms);
  logger.log_throttled("repeat", 100ms, logging::Severity::warning, "test",
                       "repeat", "code=2", t0 + 100ms);
  CHECK("LOG-001", ::dup2(restore_stderr.get(), STDERR_FILENO) >= 0);
  capture_writer.reset();
  std::array<char, 2048> output{};
  const auto output_size =
      ::read(capture_reader.get(), output.data(), output.size() - 1U);
  CHECK("LOG-001", output_size > 0);
  const std::string_view text{
      output.data(),
      output_size > 0 ? static_cast<std::size_t>(output_size) : 0U};
  CHECK("LOG-001", text.find("severity=INFO") != std::string::npos);
  CHECK("LOG-001", text.find("test") != std::string::npos);
  CHECK("LOG-002", text.find("suppressed=1") != std::string::npos);
}

} // namespace

/**
 * Execute Linux mechanism integration tests without accessing real devices.
 *
 * @return EXIT_SUCCESS when all assertions pass, otherwise EXIT_FAILURE.
 */
int main() {
  test_fd_and_poll();
  test_monotonic_timer();
  test_termination_event();
  test_serial_port();
  test_logger();
  if (failures != 0) {
    std::cerr << "linux_platform_integration failures=" << failures << '\n';
    return EXIT_FAILURE;
  }
  std::cout << "linux_platform_integration passed\n";
  return EXIT_SUCCESS;
}
