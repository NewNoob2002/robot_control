#include "platform/linux/can/interface_inhibitor.hpp"

#include <linux/can.h>
#include <linux/can/error.h>
#include <linux/can/raw.h>
#include <linux/capability.h>
#include <net/if.h>
#include <poll.h>
#include <spawn.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <limits>
#include <string>
#include <thread>
#include <utility>

extern char** environ;

namespace robot_control::platform::linux::can {
namespace {

constexpr char ready_response = 'A';
constexpr char inhibit_command = 'I';
constexpr char inhibit_response = 'D';
constexpr char release_command = 'R';
constexpr char release_response = 'R';
constexpr char error_response = 'E';
constexpr int inherited_control_fd = 3;

/** Return the bounded poll timeout before one monotonic deadline. */
int poll_timeout(const std::chrono::steady_clock::time_point deadline) noexcept {
    const auto remaining =
        std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
    return static_cast<int>(std::clamp<std::int64_t>(remaining.count() + 1, 0, std::numeric_limits<int>::max()));
}

/** Verify effective CAP_NET_ADMIN before arming the helper. */
bool has_net_admin() noexcept {
    __user_cap_header_struct header{.version = _LINUX_CAPABILITY_VERSION_3, .pid = 0};
    std::array<__user_cap_data_struct, 2> data{};
    if (::syscall(SYS_capget, &header, data.data()) != 0) {
        return false;
    }
    constexpr unsigned bit = CAP_NET_ADMIN;
    return (data[bit / 32U].effective & (1U << (bit % 32U))) != 0U;
}

/** Send one protocol byte without raising SIGPIPE. */
bool send_byte(const int fd, const char value) noexcept {
    return ::send(fd, &value, 1U, MSG_NOSIGNAL) == 1;
}

/** Set and verify the fixed physical CAN interface administratively down. */
bool set_interface_down(const std::string& interface_name) noexcept {
    if (interface_name != "can0") {
        errno = EACCES;
        return false;
    }
    const UniqueFd control{::socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0)};
    if (!control) {
        return false;
    }
    ifreq request{};
    std::memcpy(request.ifr_name, interface_name.c_str(), interface_name.size() + 1U);
    if (::ioctl(control.get(), SIOCGIFFLAGS, &request) != 0) {
        return false;
    }
    if ((request.ifr_flags & IFF_UP) != 0) {
        request.ifr_flags = static_cast<short>(request.ifr_flags & static_cast<short>(~IFF_UP));
        if (::ioctl(control.get(), SIOCSIFFLAGS, &request) != 0) {
            return false;
        }
    }
    return ::ioctl(control.get(), SIOCGIFFLAGS, &request) == 0 && (request.ifr_flags & IFF_UP) == 0;
}

/** Open a receive-only CAN error monitor for the fixed interface. */
Result<UniqueFd> open_error_monitor(const std::string& interface_name) noexcept {
    errno = 0;
    const unsigned index = ::if_nametoindex(interface_name.c_str());
    if (index == 0U) {
        return Result<UniqueFd>::failure(
            Status::from_errno("if_nametoindex", interface_name, errno == 0 ? ENODEV : errno));
    }
    UniqueFd socket{::socket(PF_CAN, SOCK_RAW | SOCK_CLOEXEC | SOCK_NONBLOCK, CAN_RAW)};
    if (!socket) {
        return Result<UniqueFd>::failure(Status::from_errno("socket(CAN_RAW)", interface_name, errno));
    }
    constexpr can_err_mask_t errors = CAN_ERR_MASK;
    if (::setsockopt(socket.get(), SOL_CAN_RAW, CAN_RAW_ERR_FILTER, &errors, sizeof(errors)) != 0
        || ::setsockopt(socket.get(), SOL_CAN_RAW, CAN_RAW_FILTER, nullptr, 0U) != 0) {
        return Result<UniqueFd>::failure(Status::from_errno("setsockopt(CAN_RAW)", interface_name, errno));
    }
    sockaddr_can address{};
    address.can_family = AF_CAN;
    address.can_ifindex = static_cast<int>(index);
    if (::bind(socket.get(), reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0) {
        return Result<UniqueFd>::failure(Status::from_errno("bind(CAN_RAW)", interface_name, errno));
    }
    return Result<UniqueFd>::success(std::move(socket));
}

} // namespace

InterfaceInhibitor::InterfaceInhibitor(UniqueFd control, const pid_t child, std::string context) noexcept
    : control_{std::move(control)}, child_{child}, context_{std::move(context)} {}

InterfaceInhibitor::~InterfaceInhibitor() {
    control_.reset();
    if (child_ > 0) {
        static_cast<void>(::waitpid(child_, nullptr, WNOHANG));
    }
}

InterfaceInhibitor::InterfaceInhibitor(InterfaceInhibitor&& other) noexcept
    : control_{std::move(other.control_)}, child_{std::exchange(other.child_, -1)},
      context_{std::move(other.context_)} {}

InterfaceInhibitor::LaunchResult InterfaceInhibitor::launch(std::string helper_path, std::string interface_name,
                                                            const std::chrono::milliseconds timeout) noexcept {
    const std::string context = "helper=" + helper_path + " interface=" + interface_name;
    if (helper_path.empty() || interface_name != "can0" || timeout <= std::chrono::milliseconds::zero()) {
        return LaunchResult::failure(Status::from_errno("interface_inhibitor_arguments", context, EINVAL));
    }
    std::array<int, 2> pair{-1, -1};
    if (::socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, pair.data()) != 0) {
        return LaunchResult::failure(Status::from_errno("socketpair", context, errno));
    }
    UniqueFd parent{pair[0]};
    UniqueFd child{pair[1]};
    posix_spawn_file_actions_t actions{};
    const int actions_error = ::posix_spawn_file_actions_init(&actions);
    if (actions_error != 0) {
        return LaunchResult::failure(Status::from_errno("posix_spawn_file_actions_init", context, actions_error));
    }
    int spawn_error = ::posix_spawn_file_actions_addclose(&actions, parent.get());
    if (spawn_error == 0) {
        spawn_error = ::posix_spawn_file_actions_adddup2(&actions, child.get(), inherited_control_fd);
    }
    std::string descriptor = std::to_string(inherited_control_fd);
    std::array<char*, 6> arguments{helper_path.data(),    const_cast<char*>("--control-fd"),
                                   descriptor.data(),     const_cast<char*>("--interface"),
                                   interface_name.data(), nullptr};
    pid_t pid = -1;
    if (spawn_error == 0) {
        spawn_error = ::posix_spawn(&pid, helper_path.c_str(), &actions, nullptr, arguments.data(), environ);
    }
    static_cast<void>(::posix_spawn_file_actions_destroy(&actions));
    if (spawn_error != 0) {
        return LaunchResult::failure(Status::from_errno("posix_spawn", context, spawn_error));
    }
    child.reset();
    InterfaceInhibitor result{std::move(parent), pid, context};
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    auto ready = result.receive(ready_response, deadline, "interface_inhibitor_ready");
    if (!ready.ok()) {
        return LaunchResult::failure(std::move(ready));
    }
    return LaunchResult::success(std::move(result));
}

Status InterfaceInhibitor::receive(const char expected, const std::chrono::steady_clock::time_point deadline,
                                   const char* const operation) noexcept {
    pollfd descriptor{.fd = control_.get(), .events = POLLIN | POLLHUP, .revents = 0};
    int result = 0;
    do {
        result = ::poll(&descriptor, 1U, poll_timeout(deadline));
    } while (result < 0 && errno == EINTR && std::chrono::steady_clock::now() < deadline);
    if (result <= 0) {
        return Status::from_errno(operation, context_, result == 0 ? ETIMEDOUT : errno);
    }
    char response = 0;
    const ssize_t received = ::recv(control_.get(), &response, 1U, MSG_TRUNC);
    if (received != 1 || response != expected) {
        return Status::from_errno(operation, context_, response == error_response ? EIO : EPROTO);
    }
    return Status::success();
}

Status InterfaceInhibitor::finish(const std::chrono::steady_clock::time_point deadline,
                                  const char* const operation) noexcept {
    control_.reset();
    while (child_ > 0 && std::chrono::steady_clock::now() < deadline) {
        int status = 0;
        const pid_t result = ::waitpid(child_, &status, WNOHANG);
        if (result == child_) {
            child_ = -1;
            if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
                return Status::success();
            }
            return Status::from_errno(operation, context_, ECHILD);
        }
        if (result < 0) {
            const int saved_errno = errno;
            child_ = -1;
            return Status::from_errno(operation, context_, saved_errno);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    return Status::from_errno(operation, context_, ETIMEDOUT);
}

Status InterfaceInhibitor::exchange(const char command, const char expected, const std::chrono::milliseconds timeout,
                                    const char* const operation) noexcept {
    if (!control_ || child_ <= 0 || timeout <= std::chrono::milliseconds::zero()) {
        return Status::from_errno(operation, context_, EINVAL);
    }
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    pollfd available{.fd = control_.get(), .events = POLLIN, .revents = 0};
    const int queued = ::poll(&available, 1U, 0);
    if (queued <= 0 && !send_byte(control_.get(), command) && errno != EPIPE) {
        return Status::from_errno(operation, context_, errno);
    }
    auto response = receive(expected, deadline, operation);
    return response.ok() ? finish(deadline, operation) : response;
}

Status InterfaceInhibitor::inhibit(const std::chrono::milliseconds timeout) noexcept {
    return exchange(inhibit_command, inhibit_response, timeout, "interface_inhibitor_inhibit");
}

Status InterfaceInhibitor::wait_inhibited(const std::chrono::milliseconds timeout) noexcept {
    if (!control_ || child_ <= 0 || timeout <= std::chrono::milliseconds::zero()) {
        return Status::from_errno("interface_inhibitor_wait", context_, EINVAL);
    }
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    auto response = receive(inhibit_response, deadline, "interface_inhibitor_wait");
    return response.ok() ? finish(deadline, "interface_inhibitor_wait") : response;
}

Status InterfaceInhibitor::release(const std::chrono::milliseconds timeout) noexcept {
    return exchange(release_command, release_response, timeout, "interface_inhibitor_release");
}

int run_interface_inhibitor(const int control_fd, const std::string& interface_name) noexcept {
    if (control_fd < 0) {
        return 2;
    }
    if (interface_name != "can0" || !has_net_admin()) {
        static_cast<void>(send_byte(control_fd, error_response));
        return 2;
    }
    int socket_type = 0;
    socklen_t socket_type_size = sizeof(socket_type);
    if (::getsockopt(control_fd, SOL_SOCKET, SO_TYPE, &socket_type, &socket_type_size) != 0
        || socket_type != SOCK_SEQPACKET) {
        static_cast<void>(send_byte(control_fd, error_response));
        return 2;
    }
    auto monitor_result = open_error_monitor(interface_name);
    if (!monitor_result.ok()) {
        static_cast<void>(send_byte(control_fd, error_response));
        return 1;
    }
    auto monitor = std::move(monitor_result).value();
    if (!send_byte(control_fd, ready_response)) {
        return set_interface_down(interface_name) ? 0 : 1;
    }
    std::array<pollfd, 2> descriptors{{{.fd = control_fd, .events = POLLIN | POLLHUP, .revents = 0},
                                       {.fd = monitor.get(), .events = POLLIN, .revents = 0}}};
    for (;;) {
        int result = 0;
        do {
            result = ::poll(descriptors.data(), descriptors.size(), -1);
        } while (result < 0 && errno == EINTR);
        if (result < 0) {
            return set_interface_down(interface_name) ? 0 : 1;
        }
        if ((descriptors[1].revents & POLLIN) != 0) {
            can_frame frame{};
            if (::recv(monitor.get(), &frame, sizeof(frame), 0) == CAN_MTU && (frame.can_id & CAN_ERR_FLAG) != 0U) {
                const bool down = set_interface_down(interface_name);
                static_cast<void>(send_byte(control_fd, down ? inhibit_response : error_response));
                return down ? 0 : 1;
            }
        }
        if ((descriptors[0].revents & (POLLHUP | POLLERR | POLLNVAL)) != 0) {
            return set_interface_down(interface_name) ? 0 : 1;
        }
        if ((descriptors[0].revents & POLLIN) != 0) {
            char command = 0;
            if (::recv(control_fd, &command, 1U, MSG_TRUNC) != 1) {
                return set_interface_down(interface_name) ? 0 : 1;
            }
            if (command == release_command) {
                static_cast<void>(send_byte(control_fd, release_response));
                return 0;
            }
            const bool down = set_interface_down(interface_name);
            static_cast<void>(send_byte(control_fd, down ? inhibit_response : error_response));
            return down ? 0 : 1;
        }
    }
}

} // namespace robot_control::platform::linux::can
