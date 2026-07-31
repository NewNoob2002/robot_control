#pragma once

#include "platform/linux/error.hpp"
#include "platform/linux/unique_fd.hpp"

#include <signal.h>

namespace robot_control::platform::linux::process {

class TerminationEvent final {
public:
  TerminationEvent(const TerminationEvent &) = delete;
  TerminationEvent &operator=(const TerminationEvent &) = delete;

  /**
   * Move signal-mask restoration and signalfd ownership.
   *
   * @param other Source event, left inactive.
   */
  TerminationEvent(TerminationEvent &&other) noexcept;

  /**
   * Move-assign signal-mask restoration and signalfd ownership.
   *
   * @param other Source event, left inactive.
   * @return This event.
   */
  TerminationEvent &operator=(TerminationEvent &&other) noexcept;

  /** Restore the creating thread's prior signal mask and close signalfd. */
  ~TerminationEvent();

  /**
   * Block SIGINT/SIGTERM in the current thread and create a nonblocking
   * signalfd.
   *
   * This must run in the process composition thread before worker threads are
   * created so they inherit the blocked mask.
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
  TerminationEvent(UniqueFd fd, sigset_t old_mask) noexcept;
  void restore() noexcept;

  UniqueFd fd_{};
  sigset_t old_mask_{};
  bool restores_mask_{false};
};

} // namespace robot_control::platform::linux::process
