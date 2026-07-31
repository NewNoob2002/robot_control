#include "service/logging/logger.hpp"

#include "service/logging/easylogger_port_status.h"

#include <elog.h>

#include <chrono>
#include <mutex>
#include <sstream>

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

uint8_t easylogger_level(const Severity severity) noexcept {
  switch (severity) {
  case Severity::debug:
    return ELOG_LVL_DEBUG;
  case Severity::info:
    return ELOG_LVL_INFO;
  case Severity::warning:
    return ELOG_LVL_WARN;
  case Severity::error:
  case Severity::critical:
    return ELOG_LVL_ERROR;
  }
  return ELOG_LVL_ERROR;
}

void initialize_easylogger() noexcept {
  static std::once_flag initialized;
  std::call_once(initialized, [] {
    static_cast<void>(elog_init());
    for (uint8_t level = ELOG_LVL_ASSERT; level < ELOG_LVL_TOTAL_NUM; ++level) {
      elog_set_fmt(level, ELOG_FMT_LVL | ELOG_FMT_TAG);
    }
    elog_start();
  });
}

} // namespace

Logger::Logger() { initialize_easylogger(); }

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
  auto position = throttles_.find(key);
  ThrottleState *state = nullptr;
  if (position == throttles_.end()) {
    if (throttles_.size() >= max_throttle_keys) {
      state = &overflow_throttle_;
    } else {
      position = throttles_.try_emplace(std::move(key)).first;
    }
  }
  if (state == nullptr) {
    state = &position->second;
  }
  if (state->initialized && interval > std::chrono::milliseconds::zero() &&
      (now - state->last_emitted) < interval) {
    ++state->suppressed;
    return;
  }
  write_line(severity, module, LineContent{event, context}, state->suppressed);
  state->last_emitted = now;
  state->suppressed = 0;
  state->initialized = true;
}

LoggerHealth Logger::health() const noexcept {
  return LoggerHealth{
      .output_failures = robot_control_elog_failure_count(),
      .last_error = robot_control_elog_last_error(),
  };
}

std::size_t Logger::throttle_key_count() const {
  const std::scoped_lock lock{mutex_};
  return throttles_.size();
}

void Logger::write_line(const Severity severity, const std::string_view module,
                        const LineContent content,
                        const std::uint64_t suppressed) {
  const auto timestamp = std::chrono::system_clock::now();
  const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(
                           timestamp.time_since_epoch())
                           .count();
  std::ostringstream line;
  line << "timestamp_unix_s=" << seconds << " severity=" << label(severity)
       << " event=\"" << content.event << '"';
  if (!content.context.empty()) {
    line << ' ' << content.context;
  }
  if (suppressed != 0) {
    line << " suppressed=" << suppressed;
  }
  const auto text = line.str();
  const std::string tag{module};
  elog_output(easylogger_level(severity), tag.c_str(), __FILE__, __func__,
              __LINE__, "%s", text.c_str());
}

} // namespace robot_control::service::logging
