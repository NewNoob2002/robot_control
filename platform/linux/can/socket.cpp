#include "platform/linux/can/socket.hpp"

#include <linux/can/raw.h>

#include <net/if.h>
#include <sys/socket.h>

#include <cerrno>
#include <cstddef>
#include <limits>
#include <utility>

namespace robot_control::platform::linux::can {

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

} // namespace robot_control::platform::linux::can
