#include "platform/linux/process/termination_event.hpp"

#include <sys/signalfd.h>

#include <cerrno>
#include <cstring>

namespace robot_control::platform::linux::process {

TerminationEvent::TerminationEvent(UniqueFd fd,
                                   const sigset_t old_mask) noexcept
    : fd_{std::move(fd)}, old_mask_{old_mask}, restores_mask_{true} {}

TerminationEvent::TerminationEvent(TerminationEvent &&other) noexcept
    : fd_{std::move(other.fd_)}, old_mask_{other.old_mask_},
      restores_mask_{other.restores_mask_} {
  other.restores_mask_ = false;
}

TerminationEvent &
TerminationEvent::operator=(TerminationEvent &&other) noexcept {
  if (this != &other) {
    restore();
    fd_ = std::move(other.fd_);
    old_mask_ = other.old_mask_;
    restores_mask_ = other.restores_mask_;
    other.restores_mask_ = false;
  }
  return *this;
}

TerminationEvent::~TerminationEvent() { restore(); }

Result<TerminationEvent> TerminationEvent::create() noexcept {
  sigset_t mask{};
  static_cast<void>(::sigemptyset(&mask));
  static_cast<void>(::sigaddset(&mask, SIGINT));
  static_cast<void>(::sigaddset(&mask, SIGTERM));

  sigset_t old_mask{};
  const int mask_result = ::pthread_sigmask(SIG_BLOCK, &mask, &old_mask);
  if (mask_result != 0) {
    return Result<TerminationEvent>::failure(
        Status::from_errno("pthread_sigmask", "SIGINT,SIGTERM", mask_result));
  }

  const int descriptor = ::signalfd(-1, &mask, SFD_CLOEXEC | SFD_NONBLOCK);
  if (descriptor < 0) {
    const int saved_errno = errno;
    static_cast<void>(::pthread_sigmask(SIG_SETMASK, &old_mask, nullptr));
    return Result<TerminationEvent>::failure(
        Status::from_errno("signalfd", "SIGINT,SIGTERM", saved_errno));
  }
  return Result<TerminationEvent>::success(
      TerminationEvent{UniqueFd{descriptor}, old_mask});
}

Result<int> TerminationEvent::consume() noexcept {
  signalfd_siginfo information{};
  const auto count = ::read(fd_.get(), &information, sizeof(information));
  if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
    return Result<int>::success(0);
  }
  if (count < 0) {
    return Result<int>::failure(
        Status::from_errno("read", "termination signalfd", errno));
  }
  if (count != static_cast<ssize_t>(sizeof(information))) {
    return Result<int>::failure(
        Status::from_errno("read", "short signalfd record", EIO));
  }
  return Result<int>::success(static_cast<int>(information.ssi_signo));
}

void TerminationEvent::restore() noexcept {
  fd_.reset();
  if (restores_mask_) {
    static_cast<void>(::pthread_sigmask(SIG_SETMASK, &old_mask_, nullptr));
    restores_mask_ = false;
  }
}

} // namespace robot_control::platform::linux::process
