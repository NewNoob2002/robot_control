#pragma once

#include "communication/canopen/stack_storage.hpp"
#include "platform/linux/process/termination_event.hpp"

extern "C" {
#include "CO_epoll_interface.h"
}

#include <chrono>
#include <memory>

namespace robot_control::communication::canopen {

/** Reason a bounded CANopen owner loop returned without an error. */
enum class LifecycleExit { deadline, sigint, sigterm, application_reset, quit };

/** Own and process the sole Linux CANopen context in normal observer mode. */
class Lifecycle final {
public:
  using CreateResult = platform::linux::Result<std::unique_ptr<Lifecycle>>;
  using RunResult = platform::linux::Result<LifecycleExit>;

  /**
   * Allocate the stack, create the monotonic epoll set, and open one existing
   * CAN interface without changing link configuration.
   *
   * @param config Validated startup configuration transferred to the owner.
   * @param termination Process-lifetime synchronous signal event borrowed by
   * this owner and required to outlive it.
   * @return Sole lifecycle owner or a context-rich startup failure.
   *
   * Thread safety: Call before creating worker threads. The returned object is
   * single-thread owned and also blocks SIGINT/SIGTERM synchronously.
   */
  [[nodiscard]] static CreateResult create(
      StackConfig config,
      platform::linux::process::TerminationEvent &termination) noexcept;

  /** Close the CAN endpoint, epoll descriptors, and upstream objects. */
  ~Lifecycle();

  Lifecycle(const Lifecycle &) = delete;
  Lifecycle &operator=(const Lifecycle &) = delete;
  Lifecycle(Lifecycle &&) = delete;
  Lifecycle &operator=(Lifecycle &&) = delete;

  /**
   * Reinitialize communication on the configured existing interface.
   *
   * @return Success after receive activation, otherwise an upstream/syscall
   * failure containing interface and node identity.
   *
   * Thread safety: Owner-thread only. This never configures or transmits on the
   * link; the process-wide transmit gate remains deny-by-default.
   */
  [[nodiscard]] platform::linux::Status reopen() noexcept;

  /**
   * Process CANopen until a monotonic deadline, termination signal, or reset.
   *
   * Communication reset and endpoint error/hangup perform one bounded reopen
   * attempt and continue only when observation has been restored.
   *
   * @param deadline Absolute steady-clock deadline.
   * @return Exit reason, or a context-rich processing/reopen failure.
   *
   * Thread safety: Owner-thread only; no concurrent init/process calls allowed.
   */
  [[nodiscard]] RunResult
  run_until(std::chrono::steady_clock::time_point deadline) noexcept;

private:
  /** Construct an inactive owner from already claimed resources. */
  Lifecycle(std::unique_ptr<StackStorage> storage,
            platform::linux::process::TerminationEvent &termination) noexcept;

  /** Return true when the current epoll event reports endpoint loss. */
  [[nodiscard]] bool endpoint_lost() const noexcept;

  std::unique_ptr<StackStorage> storage_{};
  platform::linux::process::TerminationEvent *termination_{nullptr};
  CO_epoll_t epoll_{};
};

} // namespace robot_control::communication::canopen
