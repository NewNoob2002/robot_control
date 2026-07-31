#include "service/logging/logger.hpp"

#include <chrono>
#include <iomanip>

namespace robot_control::service::logging {
namespace {

/**
 * Convert severity to a stable journal-friendly label.
 *
 * @param severity Typed severity.
 * @return Static label.
 */
std::string_view label(const Severity severity) noexcept {
  switch (severity) {
  case Severity::debug:
    return "DEBUG";
  case Severity::info:
    return "INFO";
  case Severity::warning:
    return "WARNING";
  case Severity::error:
    return "ERROR";
  case Severity::critical:
    return "CRITICAL";
  }
  return "UNKNOWN";
}

} // namespace

Logger::Logger(std::ostream &output) noexcept : output_{output} {}

void Logger::log(const Severity severity, const std::string_view module,
                 const std::string_view event, const std::string_view context) {
  const std::scoped_lock lock{mutex_};
  write_line(severity, module, LineContent{event, context}, 0);
}

void Logger::log_throttled(std::string key,
                           const std::chrono::milliseconds interval,
                           const Severity severity,
                           const std::string_view module,
                           const std::string_view event,
                           const std::string_view context,
                           const std::chrono::steady_clock::time_point now) {
  const std::scoped_lock lock{mutex_};
  auto &state = throttles_[std::move(key)];
  if (state.initialized && interval > std::chrono::milliseconds::zero() &&
      (now - state.last_emitted) < interval) {
    ++state.suppressed;
    return;
  }
  write_line(severity, module, LineContent{event, context}, state.suppressed);
  state.last_emitted = now;
  state.suppressed = 0;
  state.initialized = true;
}

void Logger::write_line(const Severity severity, const std::string_view module,
                        const LineContent content,
                        const std::uint64_t suppressed) {
  const auto timestamp = std::chrono::system_clock::now();
  const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(
                           timestamp.time_since_epoch())
                           .count();
  output_ << "timestamp_unix_s=" << seconds << " severity=" << label(severity)
          << " module=" << std::quoted(module)
          << " event=" << std::quoted(content.event);
  if (!content.context.empty()) {
    output_ << ' ' << content.context;
  }
  if (suppressed != 0) {
    output_ << " suppressed=" << suppressed;
  }
  output_ << '\n';
}

} // namespace robot_control::service::logging
