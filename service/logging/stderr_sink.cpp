#include "service/logging/stderr_sink.hpp"
#include "service/logging/stderr_sink_status.h"

#include <atomic>
#include <cerrno>
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>

namespace robot_control::service::logging {
namespace {

// Process-wide atomics retain the existing C health API and aggregate all stderr writers.
// They never grant control authority; reset is restricted to quiescent test setup.
std::atomic<std::uint64_t> g_failure_count{0};
std::atomic<int> g_last_error{0};

/** Record an output failure without recursively emitting a log. */
void record_error(int error_number) noexcept {
    g_last_error.store(error_number, std::memory_order_relaxed);
    g_failure_count.fetch_add(1U, std::memory_order_relaxed);
}

} // namespace

std::uint64_t StderrSink::failure_count() const noexcept {
    return g_failure_count.load(std::memory_order_relaxed);
}

int StderrSink::last_error() const noexcept {
    return g_last_error.load(std::memory_order_relaxed);
}

void StderrSink::reset_health() noexcept {
    g_failure_count.store(0U, std::memory_order_relaxed);
    g_last_error.store(0, std::memory_order_relaxed);
}

void StderrSink::sink_it_(const spdlog::details::log_msg& msg) {
    spdlog::memory_buf_t formatted;
    formatter_->format(msg, formatted);

    const char* log = formatted.data();
    std::size_t size = formatted.size();

    int output_fd = -1;
    unsigned int interrupted = 0U;
    while (size > 0U) {
        const auto count = output_fd < 0 ? ::send(STDERR_FILENO, log, size, MSG_DONTWAIT | MSG_NOSIGNAL)
                                         : ::write(output_fd, log, size);
        if (count < 0 && output_fd < 0 && errno == ENOTSOCK) {
            output_fd = ::open("/proc/self/fd/2", O_WRONLY | O_NONBLOCK | O_CLOEXEC | O_APPEND);
            if (output_fd < 0) {
                record_error(errno);
                break;
            }
            continue;
        }
        if (count > 0) {
            log += count;
            size -= static_cast<std::size_t>(count);
            continue;
        }
        if (count < 0 && errno == EINTR && interrupted < 8U) {
            ++interrupted;
            continue;
        }
        record_error(count == 0 ? EIO : errno);
        break;
    }
    if (output_fd >= 0 && ::close(output_fd) != 0) {
        record_error(errno);
    }
}

void StderrSink::flush_() {
    // Stderr write/send writes directly to fd, no userspace buffering to flush.
}

} // namespace robot_control::service::logging

extern "C" {

/** Return the process-wide failure count through the retained C compatibility API. */
uint64_t robot_control_elog_failure_count(void) {
    return robot_control::service::logging::g_failure_count.load(std::memory_order_relaxed);
}

/** Return the last raw errno through the retained C compatibility API. */
int robot_control_elog_last_error(void) {
    return robot_control::service::logging::g_last_error.load(std::memory_order_relaxed);
}

/** Reset compatibility diagnostics only when output is quiescent during test setup. */
void robot_control_elog_reset_health(void) {
    robot_control::service::logging::g_failure_count.store(0U, std::memory_order_relaxed);
    robot_control::service::logging::g_last_error.store(0, std::memory_order_relaxed);
}
}
