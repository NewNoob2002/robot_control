#pragma once
#include <cstring>
#include <fcntl.h>
#include <net/if.h>
#include <net/if_arp.h>
#include <stdexcept>
#include <string>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include "platform/linux/unique_fd.hpp"

namespace robot_control::app {
/** Validate the selected existing SocketCAN link and hold a cooperative per-interface app lock; no link changes. */
inline platform::linux::UniqueFd check_interface(const std::string& name) {
    platform::linux::UniqueFd socket{::socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0)};
    if (!socket)
        throw std::runtime_error("CAN interface inspection socket failed");
    ifreq request{};
    std::memcpy(request.ifr_name, name.c_str(), name.size() + 1);
    if (::ioctl(socket.get(), SIOCGIFHWADDR, &request) != 0 || request.ifr_hwaddr.sa_family != ARPHRD_CAN)
        throw std::runtime_error("interface=" + name + " is missing or not SocketCAN");
    if (::ioctl(socket.get(), SIOCGIFFLAGS, &request) != 0 || !(request.ifr_flags & IFF_UP))
        throw std::runtime_error("interface=" + name + " is down; configure bitrate/link-up outside the app");
    const auto path = "/tmp/robot-control-interface-" + name + ".lock";
    platform::linux::UniqueFd lock{::open(path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600)};
    if (!lock || ::flock(lock.get(), LOCK_EX | LOCK_NB) != 0)
        throw std::runtime_error("interface=" + name + " app lock unavailable: " + std::strerror(errno));
    return lock;
}
} // namespace robot_control::app
