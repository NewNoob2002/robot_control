#pragma once

#include "platform/linux/error.hpp"
#include "platform/linux/unique_fd.hpp"

#include <signal.h>

namespace robot_control::platform::linux::process {

class TerminationEvent final {
public:
  TerminationEvent(const TerminationEvent &) = delete;
  TerminationEvent &operator=(const TerminationEvent &) = delete;

  TerminationEvent(TerminationEvent &&) noexcept = default;
  TerminationEvent &operator=(TerminationEvent &&) noexcept = default;
  ~TerminationEvent() = default;

  /**
   * Block SIGINT/SIGTERM in the current thread and create a nonblocking
   * signalfd.
   *
   * This must run in the process composition thread before worker threads are
   * created so they inherit the blocked mask. The mask remains blocked for the
   * lifetime of the process; destruction only closes the descriptor.
   *
   * @return Owned event or context-rich pthread/signalfd failure.
   */
  [[nodiscard]] static Result<TerminationEvent> create() noexcept;

  /**
   * Return the borrowed signalfd for poll/epoll registration.
   *
   * @return Valid descriptor while this object is alive.
   */
  [[nodiscard]] int fd() const noexcept { return fd_.get(); }

  /**
   * Consume one queued termination signal.
   *
   * @return Signal number, zero when no event is queued, or read failure.
   *
   * Thread safety: Single-consumer only.
   */
  [[nodiscard]] Result<int> consume() noexcept;

private:
  explicit TerminationEvent(UniqueFd fd) noexcept;

  UniqueFd fd_{};
};

} // namespace robot_control::platform::linux::process
