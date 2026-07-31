#include "platform/linux/process/termination_event.hpp"

#include <sys/signalfd.h>

#include <cerrno>
#include <cstring>

namespace robot_control::platform::linux::process {

TerminationEvent::TerminationEvent(UniqueFd fd) noexcept : fd_{std::move(fd)} {}

Result<TerminationEvent> TerminationEvent::create() noexcept {
  sigset_t mask{};
  if (::sigemptyset(&mask) != 0 || ::sigaddset(&mask, SIGINT) != 0 ||
      ::sigaddset(&mask, SIGTERM) != 0) {
    return Result<TerminationEvent>::failure(
        Status::from_errno("signal set", "SIGINT,SIGTERM", errno));
  }

  sigset_t old_mask{};
  const int mask_result = ::pthread_sigmask(SIG_BLOCK, &mask, &old_mask);
  if (mask_result != 0) {
    return Result<TerminationEvent>::failure(
        Status::from_errno("pthread_sigmask", "SIGINT,SIGTERM", mask_result));
  }

  const int descriptor = ::signalfd(-1, &mask, SFD_CLOEXEC | SFD_NONBLOCK);
  if (descriptor < 0) {
    const int saved_errno = errno;
    const int restore_result =
        ::pthread_sigmask(SIG_SETMASK, &old_mask, nullptr);
    if (restore_result != 0) {
      return Result<TerminationEvent>::failure(Status::from_errno(
          "pthread_sigmask", "rollback after signalfd failure",
          restore_result));
    }
    return Result<TerminationEvent>::failure(
        Status::from_errno("signalfd", "SIGINT,SIGTERM", saved_errno));
  }
  return Result<TerminationEvent>::success(
      TerminationEvent{UniqueFd{descriptor}});
}

Result<int> TerminationEvent::consume() noexcept {
  signalfd_siginfo information{};
  ssize_t count = 0;
  do {
    count = ::read(fd_.get(), &information, sizeof(information));
  } while (count < 0 && errno == EINTR);
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

} // namespace robot_control::platform::linux::process
