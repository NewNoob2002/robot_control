#include "platform/linux/can/socket.hpp"

#include "platform/linux/io/poll_wait.hpp"

#include <linux/can/raw.h>

#include <net/if.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstddef>
#include <limits>
#include <string_view>
#include <utility>

namespace robot_control::platform::linux::can {
namespace {

/** Add bound-interface identity to a lower-level failure. */
Status with_interface_context(const Status &status,
                              const std::string &interface_context) {
  std::string context = interface_context;
  if (!status.context.empty()) {
    context += " " + status.context;
  }
  return Status::from_errno(status.operation, std::move(context),
                            status.error.value());
}

/** Validate a timeout and calculate one absolute monotonic deadline. */
Result<std::chrono::steady_clock::time_point>
io_deadline(const std::chrono::milliseconds timeout,
            const std::string_view operation,
            const std::string &interface_context) noexcept {
  if (timeout < std::chrono::milliseconds::zero()) {
    return Result<std::chrono::steady_clock::time_point>::failure(
        Status::from_errno(std::string{operation},
                           interface_context + " negative timeout", EINVAL));
  }
  const auto now = std::chrono::steady_clock::now();
  const auto maximum_timeout =
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::time_point::max() - now);
  if (timeout > maximum_timeout) {
    return Result<std::chrono::steady_clock::time_point>::failure(
        Status::from_errno(std::string{operation},
                           interface_context + " timeout overflow", EOVERFLOW));
  }
  return Result<std::chrono::steady_clock::time_point>::success(
      now +
      std::chrono::duration_cast<std::chrono::steady_clock::duration>(timeout));
}

} // namespace

CanSocket::CanSocket(UniqueFd fd, std::string interface_name,
                     const unsigned int interface_index) noexcept
    : fd_{std::move(fd)}, interface_name_{std::move(interface_name)},
      interface_index_{interface_index} {}

Result<CanSocket> CanSocket::open(std::string interface_name,
                                  const CanSocketConfig config) noexcept {
  const std::string context = "interface=" + interface_name;
  if (interface_name.empty()) {
    return Result<CanSocket>::failure(
        Status::from_errno("validate_interface_name", context, EINVAL));
  }
  if (interface_name.size() >= IFNAMSIZ) {
    return Result<CanSocket>::failure(
        Status::from_errno("validate_interface_name", context, ENAMETOOLONG));
  }

  const int descriptor =
      ::socket(PF_CAN, SOCK_RAW | SOCK_NONBLOCK | SOCK_CLOEXEC, CAN_RAW);
  if (descriptor < 0) {
    const int saved_errno = errno;
    return Result<CanSocket>::failure(
        Status::from_errno("socket", context, saved_errno));
  }
  UniqueFd fd{descriptor};

  const unsigned int interface_index = ::if_nametoindex(interface_name.c_str());
  if (interface_index == 0U) {
    const int saved_errno = errno;
    return Result<CanSocket>::failure(
        Status::from_errno("if_nametoindex", context, saved_errno));
  }

  if (config.filters.has_value()) {
    constexpr std::size_t maximum_filter_count =
        static_cast<std::size_t>(std::numeric_limits<socklen_t>::max()) /
        sizeof(::can_filter);
    if (config.filters->size() > maximum_filter_count) {
      return Result<CanSocket>::failure(
          Status::from_errno("setsockopt(CAN_RAW_FILTER)", context, EOVERFLOW));
    }
    const auto filter_bytes =
        static_cast<socklen_t>(config.filters->size() * sizeof(::can_filter));
    const void *filter_data =
        config.filters->empty()
            ? nullptr
            : static_cast<const void *>(config.filters->data());
    if (::setsockopt(fd.get(), SOL_CAN_RAW, CAN_RAW_FILTER, filter_data,
                     filter_bytes) != 0) {
      const int saved_errno = errno;
      return Result<CanSocket>::failure(Status::from_errno(
          "setsockopt(CAN_RAW_FILTER)", context, saved_errno));
    }
  }

  static_assert(sizeof(can_err_mask_t) <=
                std::numeric_limits<socklen_t>::max());
  if (::setsockopt(fd.get(), SOL_CAN_RAW, CAN_RAW_ERR_FILTER,
                   &config.error_mask,
                   static_cast<socklen_t>(sizeof(config.error_mask))) != 0) {
    const int saved_errno = errno;
    return Result<CanSocket>::failure(Status::from_errno(
        "setsockopt(CAN_RAW_ERR_FILTER)", context, saved_errno));
  }

  sockaddr_can address{};
  address.can_family = AF_CAN;
  address.can_ifindex = static_cast<int>(interface_index);
  if (::bind(fd.get(), reinterpret_cast<const sockaddr *>(&address),
             static_cast<socklen_t>(sizeof(address))) != 0) {
    const int saved_errno = errno;
    return Result<CanSocket>::failure(
        Status::from_errno("bind", context, saved_errno));
  }

  return Result<CanSocket>::success(
      CanSocket{std::move(fd), std::move(interface_name), interface_index});
}

Status CanSocket::send(const ClassicCanFrame &frame,
                       const std::chrono::milliseconds timeout,
                       const int cancellation_fd) noexcept {
  const std::string context = "interface=" + interface_name_;
  const auto encoded = encode_classic_frame(frame);
  if (!encoded.ok()) {
    return with_interface_context(encoded.status(), context);
  }

  const auto deadline = io_deadline(timeout, "send", context);
  if (!deadline.ok()) {
    return deadline.status();
  }
  if (fd_.get() < 0) {
    return Status::from_errno("send", context, EBADF);
  }

  static_assert(sizeof(::can_frame) == CAN_MTU);
  while (true) {
    const auto event =
        io::wait_writable_until(fd_.get(), deadline.value(), cancellation_fd);
    if (!event.ok()) {
      return with_interface_context(event.status(), context);
    }
    if (event.value().cancelled) {
      return Status::from_errno("send", context + " cancelled", ECANCELED);
    }
    if (!event.value().writable) {
      if (event.value().error || event.value().hangup) {
        return Status::from_errno("poll", context + " socket failure", EIO);
      }
      return Status::from_errno("send", context + " timeout", ETIMEDOUT);
    }

    const ssize_t count = ::write(fd_.get(), &encoded.value(), CAN_MTU);
    if (count == static_cast<ssize_t>(CAN_MTU)) {
      return Status::success();
    }
    if (count < 0) {
      const int saved_errno = errno;
      if (saved_errno == EINTR || saved_errno == EAGAIN ||
          saved_errno == EWOULDBLOCK) {
        continue;
      }
      return Status::from_errno("write", context, saved_errno);
    }
    return Status::from_errno("write",
                              context + " expected=" + std::to_string(CAN_MTU) +
                                  " actual=" + std::to_string(count),
                              EIO);
  }
}

Result<std::optional<ClassicCanFrame>>
CanSocket::receive(const std::chrono::milliseconds timeout,
                   const int cancellation_fd) noexcept {
  const std::string context = "interface=" + interface_name_;
  const auto deadline = io_deadline(timeout, "receive", context);
  if (!deadline.ok()) {
    return Result<std::optional<ClassicCanFrame>>::failure(deadline.status());
  }
  if (fd_.get() < 0) {
    return Result<std::optional<ClassicCanFrame>>::failure(
        Status::from_errno("receive", context, EBADF));
  }

  static_assert(sizeof(::can_frame) == CAN_MTU);
  while (true) {
    const auto event =
        io::wait_readable_until(fd_.get(), deadline.value(), cancellation_fd);
    if (!event.ok()) {
      return Result<std::optional<ClassicCanFrame>>::failure(
          with_interface_context(event.status(), context));
    }
    if (event.value().cancelled) {
      return Result<std::optional<ClassicCanFrame>>::failure(
          Status::from_errno("receive", context + " cancelled", ECANCELED));
    }
    if (!event.value().readable) {
      if (event.value().error || event.value().hangup) {
        return Result<std::optional<ClassicCanFrame>>::failure(
            Status::from_errno("poll", context + " socket failure", EIO));
      }
      return Result<std::optional<ClassicCanFrame>>::success(std::nullopt);
    }

    ::can_frame frame{};
    const ssize_t count = ::read(fd_.get(), &frame, CAN_MTU);
    if (count == static_cast<ssize_t>(CAN_MTU)) {
      auto decoded = decode_classic_frame(frame);
      if (!decoded.ok()) {
        return Result<std::optional<ClassicCanFrame>>::failure(
            with_interface_context(decoded.status(), context));
      }
      return Result<std::optional<ClassicCanFrame>>::success(
          std::optional<ClassicCanFrame>{std::move(decoded).value()});
    }
    if (count < 0) {
      const int saved_errno = errno;
      if (saved_errno == EINTR || saved_errno == EAGAIN ||
          saved_errno == EWOULDBLOCK) {
        continue;
      }
      return Result<std::optional<ClassicCanFrame>>::failure(
          Status::from_errno("read", context, saved_errno));
    }
    return Result<std::optional<ClassicCanFrame>>::failure(
        Status::from_errno("read",
                           context + " expected=" + std::to_string(CAN_MTU) +
                               " actual=" + std::to_string(count),
                           EIO));
  }
}

} // namespace robot_control::platform::linux::can
