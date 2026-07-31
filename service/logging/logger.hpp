#pragma once

#include <chrono>
#include <cstdint>
#include <mutex>
#include <ostream>
#include <string>
#include <string_view>
#include <unordered_map>

namespace robot_control::service::logging {

enum class Severity : std::uint8_t {
  debug = 0,
  info,
  warning,
  error,
  critical,
};

class Logger final {
public:
  /**
   * Construct a structured logger writing to a caller-owned stream.
   *
   * @param output Stream which must outlive the logger.
   */
  explicit Logger(std::ostream &output) noexcept;

  /**
   * Emit one structured line.
   *
   * @param severity Event severity.
   * @param module Stable module tag.
   * @param event Stable event name.
   * @param context Preformatted key-value context without secrets.
   *
   * Thread safety: Internally serialized. The output stream must not be written
   * independently without external synchronization.
   */
  void log(Severity severity, std::string_view module, std::string_view event,
           std::string_view context);

  /**
   * Emit at most one event per key and interval.
   *
   * The next emitted line includes the number of suppressed repetitions.
   *
   * @param key Stable throttle identity.
   * @param interval Positive suppression interval.
   * @param severity Event severity.
   * @param module Stable module tag.
   * @param event Stable event name.
   * @param context Preformatted key-value context without secrets.
   * @param now Injected steady-clock time for deterministic testing.
   *
   * Thread safety: Internally serialized.
   */
  void log_throttled(std::string key, std::chrono::milliseconds interval,
                     Severity severity, std::string_view module,
                     std::string_view event, std::string_view context,
                     std::chrono::steady_clock::time_point now);

private:
  struct ThrottleState {
    std::chrono::steady_clock::time_point last_emitted{};
    std::uint64_t suppressed{0};
    bool initialized{false};
  };

  struct LineContent {
    std::string_view event;
    std::string_view context;
  };

  /** Emit one line while the logger mutex is already held. */
  void write_line(Severity severity, std::string_view module,
                  LineContent content, std::uint64_t suppressed);

  std::ostream &output_;
  std::mutex mutex_{};
  std::unordered_map<std::string, ThrottleState> throttles_{};
};

} // namespace robot_control::service::logging
