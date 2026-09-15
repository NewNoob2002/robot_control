#include "platform/linux/can/interface_inhibitor.hpp"

#include <linux/can.h>
#include <linux/can/error.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <cstring>
#include <iostream>
#include <string_view>
#include <thread>

namespace {

using namespace std::chrono_literals;
using robot_control::platform::linux::can::InterfaceInhibitor;

int failures = 0;

#define CHECK(condition)                                                                                               \
    do {                                                                                                               \
        if (!(condition)) {                                                                                            \
            std::cerr << __FILE__ << ':' << __LINE__ << " failed: " #condition << '\n';                                \
            ++failures;                                                                                                \
        }                                                                                                              \
    } while (false)

/** Read whether the namespace-local test interface is administratively up. */
bool interface_up() {
    const int fd = ::socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    CHECK(fd >= 0);
    if (fd < 0) {
        return false;
    }
    ifreq request{};
    std::strncpy(request.ifr_name, "can0", IFNAMSIZ - 1U);
    const bool result = ::ioctl(fd, SIOCGIFFLAGS, &request) == 0 && (request.ifr_flags & IFF_UP) != 0;
    CHECK(::close(fd) == 0);
    return result;
}

/** Restore only the namespace-local virtual test interface. */
void restore_interface() {
    const int fd = ::socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    CHECK(fd >= 0);
    if (fd < 0) {
        return;
    }
    ifreq request{};
    std::strncpy(request.ifr_name, "can0", IFNAMSIZ - 1U);
    CHECK(::ioctl(fd, SIOCGIFFLAGS, &request) == 0);
    request.ifr_flags = static_cast<short>(request.ifr_flags | IFF_UP);
    CHECK(::ioctl(fd, SIOCSIFFLAGS, &request) == 0);
    CHECK(::close(fd) == 0);
}

/** Inject one controller error into vcan without any normal CAN request. */
void inject_error() {
    const int fd = ::socket(PF_CAN, SOCK_RAW | SOCK_CLOEXEC, CAN_RAW);
    CHECK(fd >= 0);
    if (fd < 0) {
        return;
    }
    sockaddr_can address{};
    address.can_family = AF_CAN;
    address.can_ifindex = static_cast<int>(::if_nametoindex("can0"));
    CHECK(address.can_ifindex > 0);
    CHECK(::bind(fd, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == 0);
    can_frame frame{};
    frame.can_id = CAN_ERR_FLAG | CAN_ERR_CRTL;
    frame.can_dlc = CAN_ERR_DLC;
    frame.data[1] = CAN_ERR_CRTL_TX_WARNING;
    frame.data[6] = 96U;
    CHECK(::write(fd, &frame, CAN_MTU) == CAN_MTU);
    CHECK(::close(fd) == 0);
}

/** Verify successful cleanup explicitly releases the fail-closed guard. */
void test_release(const char* helper) {
    auto guard = InterfaceInhibitor::launch(helper, "can0", 1s);
    CHECK(guard.ok());
    if (!guard.ok()) {
        return;
    }
    CHECK(std::move(guard).value().release(1s).ok());
    CHECK(interface_up());
}

/** Verify an explicit fault request closes the interface and acknowledges it. */
void test_explicit_inhibit(const char* helper) {
    auto guard = InterfaceInhibitor::launch(helper, "can0", 1s);
    CHECK(guard.ok());
    if (!guard.ok()) {
        return;
    }
    CHECK(std::move(guard).value().inhibit(1s).ok());
    CHECK(!interface_up());
    restore_interface();
}

/** Verify loss of the application-side control socket fails closed. */
void test_application_exit(const char* helper) {
    {
        auto guard = InterfaceInhibitor::launch(helper, "can0", 1s);
        CHECK(guard.ok());
    }
    for (unsigned attempt = 0U; attempt < 100U && interface_up(); ++attempt) {
        std::this_thread::sleep_for(2ms);
    }
    CHECK(!interface_up());
    restore_interface();
}

/** Verify the helper observes CAN error frames independently of the application. */
void test_error_monitor(const char* helper) {
    auto guard = InterfaceInhibitor::launch(helper, "can0", 1s);
    CHECK(guard.ok());
    if (!guard.ok()) {
        return;
    }
    auto active = std::move(guard).value();
    inject_error();
    CHECK(active.wait_inhibited(1s).ok());
    CHECK(!interface_up());
    restore_interface();
}

/** Reject a truncated release packet instead of disarming the monitor. */
void test_malformed_release() {
    int pair[2]{-1, -1};
    CHECK(::socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, pair) == 0);
    if (pair[0] < 0) {
        return;
    }
    const pid_t child = ::fork();
    CHECK(child >= 0);
    if (child == 0) {
        static_cast<void>(::close(pair[0]));
        ::_exit(robot_control::platform::linux::can::run_interface_inhibitor(pair[1], "can0"));
    }
    CHECK(::close(pair[1]) == 0);
    if (child > 0) {
        const timeval timeout{.tv_sec = 1, .tv_usec = 0};
        CHECK(::setsockopt(pair[0], SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) == 0);
        char ready = 0;
        CHECK(::recv(pair[0], &ready, 1U, 0) == 1 && ready == 'A');
        CHECK(::send(pair[0], "RX", 2U, MSG_NOSIGNAL) == 2);
        int status = 0;
        CHECK(::waitpid(child, &status, 0) == child);
        CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);
        CHECK(!interface_up());
        restore_interface();
    }
    CHECK(::close(pair[0]) == 0);
}

} // namespace

int main(const int argc, char** argv) {
    if (argc != 2 || !std::string_view{argv[1]}.ends_with("robot-control-can-interface-inhibitor")) {
        std::cerr << "Expected the interface inhibitor helper path\n";
        return 2;
    }
    CHECK(interface_up());
    test_release(argv[1]);
    test_explicit_inhibit(argv[1]);
    test_application_exit(argv[1]);
    test_error_monitor(argv[1]);
    test_malformed_release();
    CHECK(!InterfaceInhibitor::launch(argv[1], "vcan0", 100ms).ok());
    CHECK(interface_up());
    return failures == 0 ? 0 : 1;
}
