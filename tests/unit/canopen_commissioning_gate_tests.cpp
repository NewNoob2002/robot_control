#include "communication/canopen/commissioning_gate.h"

#include <linux/can.h>
#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <cstdint>
#include <iostream>

namespace {
int failures = 0;
#define CHECK(expression)                                                                                              \
    do {                                                                                                               \
        if (!(expression)) {                                                                                           \
            ++failures;                                                                                                \
            std::cerr << #expression << " failed\n";                                                                   \
        }                                                                                                              \
    } while (false)

can_frame sdo(const robot_control_canopen_sdo_object_t object) {
    can_frame frame{};
    frame.can_id = 0x601U;
    frame.can_dlc = 8U;
    frame.data[0] = 0x40U;
    frame.data[1] = static_cast<std::uint8_t>(object.index);
    frame.data[2] = static_cast<std::uint8_t>(object.index >> 8U);
    frame.data[3] = object.subindex;
    return frame;
}
} // namespace

int main() {
    int sockets[2]{};
    CHECK(::socketpair(AF_UNIX, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0, sockets) == 0);
    const std::array allowed{std::pair{0x1000U, 0U}, std::pair{0x1001U, 0U}, std::pair{0x1009U, 0U},
                             std::pair{0x100AU, 0U}, std::pair{0x1018U, 1U}, std::pair{0x1018U, 2U},
                             std::pair{0x2031U, 0U}, std::pair{0x2032U, 3U}, std::pair{0x2035U, 0U},
                             std::pair{0x603FU, 0U}, std::pair{0x6041U, 0U}, std::pair{0x6061U, 0U}};
    for (const auto& [index, subindex] : allowed) {
        const robot_control_canopen_sdo_object_t object{.index = static_cast<std::uint16_t>(index),
                                                        .subindex = static_cast<std::uint8_t>(subindex)};
        const auto frame = sdo(object);
        CHECK(robot_control_canopen_authorize_sdo_upload(object));
        const auto sent = robot_control_canopen_commissioning_transmit(sockets[0], &frame, CAN_MTU, MSG_DONTWAIT);
        CHECK(sent == CAN_MTU);
        can_frame received{};
        CHECK(::recv(sockets[1], &received, CAN_MTU, 0) == CAN_MTU);
        CHECK(received.can_id == frame.can_id);
    }
    for (const auto& [index, subindex] :
         std::array{std::pair{0x6040U, 0U}, std::pair{0x6060U, 0U}, std::pair{0x60FFU, 0U}, std::pair{0x2010U, 0U},
                    std::pair{0x1018U, 0U}, std::pair{0x2032U, 2U}}) {
        CHECK(!robot_control_canopen_authorize_sdo_upload(
            {.index = static_cast<std::uint16_t>(index), .subindex = static_cast<std::uint8_t>(subindex)}));
    }
    CHECK(robot_control_canopen_authorize_nmt(0x02U));
    can_frame stopped{};
    stopped.can_dlc = 2U;
    stopped.data[0] = 0x02U;
    stopped.data[1] = 1U;
    CHECK(robot_control_canopen_commissioning_transmit(sockets[0], &stopped, CAN_MTU, MSG_DONTWAIT) == CAN_MTU);
    CHECK(robot_control_canopen_authorize_nmt(0x80U));
    stopped.data[0] = 0x01U;
    errno = 0;
    CHECK(robot_control_canopen_commissioning_transmit(sockets[0], &stopped, CAN_MTU, MSG_DONTWAIT) == -1);
    CHECK(errno == EACCES);
    CHECK(!robot_control_canopen_authorize_nmt(0x01U));
    CHECK(!robot_control_canopen_authorize_nmt(0x81U));

    const auto retry = sdo({.index = 0x1001U, .subindex = 0U});
    ssize_t fill_result = 0;
    do {
        errno = 0;
        fill_result = ::send(sockets[0], &retry, CAN_MTU, MSG_DONTWAIT);
    } while (fill_result == CAN_MTU);
    CHECK(fill_result == -1);
    const int fill_error = fill_result < 0 ? errno : 0;
    CHECK(fill_error == EAGAIN || fill_error == EWOULDBLOCK);
    CHECK(robot_control_canopen_authorize_sdo_upload({.index = 0x1001U, .subindex = 0U}));
    CHECK(robot_control_canopen_commissioning_transmit(sockets[0], &retry, CAN_MTU, MSG_DONTWAIT) == -1);
    CHECK(errno == EAGAIN || errno == EWOULDBLOCK);
    can_frame drained{};
    CHECK(::recv(sockets[1], &drained, CAN_MTU, 0) == CAN_MTU);
    CHECK(robot_control_canopen_commissioning_transmit(sockets[0], &retry, CAN_MTU, MSG_DONTWAIT) == CAN_MTU);

    CHECK(::close(sockets[0]) == 0);
    CHECK(::close(sockets[1]) == 0);
    return failures == 0 ? 0 : 1;
}
