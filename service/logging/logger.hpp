#pragma once

#include <chrono>
#include <cstdint>
#include <mutex>
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

/** Snapshot of non-recursive EasyLogger output health. */
struct LoggerHealth {
  std::uint64_t output_failures{0};
  int last_error{0};
};

class Logger final {
public:
  /**
   * Construct a structured logger backed by EasyLogger stderr output.
   */
  Logger();

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
   * At most 256 stable keys are retained. New keys beyond that bound are
   * emitted without throttling so unrelated events never suppress each other.
   * Control-cycle callers must use a fixed, predeclared key set.
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

  /**
   * Return non-recursive EasyLogger port failure diagnostics.
   *
   * @return Atomic process-wide health snapshot.
   */
  [[nodiscard]] LoggerHealth health() const noexcept;

  /**
   * Return the bounded number of tracked throttle identities.
   *
   * @return Value in the inclusive range 0..256.
   */
  [[nodiscard]] std::size_t throttle_key_count() const;

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

  mutable std::mutex mutex_{};
  std::unordered_map<std::string, ThrottleState> throttles_{};
  static constexpr std::size_t max_throttle_keys = 256;
};

} // namespace robot_control::service::logging
