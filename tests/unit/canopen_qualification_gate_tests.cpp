#include "communication/canopen/qualification_gate.h"

#include <linux/can.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
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

/** Build one expected node-1 expedited request. */
can_frame sdo(const std::uint8_t command, const std::uint16_t index, const std::uint8_t subindex,
              const std::uint32_t value = 0U) {
    can_frame frame{};
    frame.can_id = 0x601U;
    frame.can_dlc = 8U;
    const std::array data{command,
                          static_cast<std::uint8_t>(index),
                          static_cast<std::uint8_t>(index >> 8U),
                          subindex,
                          static_cast<std::uint8_t>(value),
                          static_cast<std::uint8_t>(value >> 8U),
                          static_cast<std::uint8_t>(value >> 16U),
                          static_cast<std::uint8_t>(value >> 24U)};
    std::copy(data.begin(), data.end(), std::begin(frame.data));
    return frame;
}

/** Submit one authorized frame and consume its peer copy. */
void expect_sent(const std::array<int, 2>& sockets, const can_frame& frame) {
    CHECK(robot_control_canopen_qualification_transmit(sockets[0], &frame, CAN_MTU, MSG_DONTWAIT) == CAN_MTU);
    can_frame received{};
    CHECK(::recv(sockets[1], &received, CAN_MTU, 0) == CAN_MTU);
    CHECK(received.can_id == frame.can_id);
    CHECK(received.can_dlc == frame.can_dlc);
    CHECK(std::equal(std::begin(received.data), std::end(received.data), std::begin(frame.data)));
}

/** Require one mismatched frame to consume and clear authorization. */
void expect_rejected(const int sender, can_frame frame) {
    errno = 0;
    CHECK(robot_control_canopen_qualification_transmit(sender, &frame, CAN_MTU, MSG_DONTWAIT) == -1);
    CHECK(errno == EACCES);
}
} // namespace

/** Exercise the exact one-frame P6.3 transmit allowlist. */
int main() {
    std::array<int, 2> sockets{};
    CHECK(::socketpair(AF_UNIX, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0, sockets.data()) == 0);

    for (const std::uint8_t command : std::array<std::uint8_t, 3>{0x01U, 0x02U, 0x80U}) {
        CHECK(robot_control_canopen_qualification_authorize_nmt(command));
        can_frame frame{};
        frame.can_dlc = 2U;
        frame.data[0] = command;
        frame.data[1] = 1U;
        expect_sent(sockets, frame);
        expect_rejected(sockets[0], frame);
    }
    CHECK(!robot_control_canopen_qualification_authorize_nmt(0x81U));

    CHECK(robot_control_canopen_qualification_authorize_velocity_mode());
    expect_sent(sockets, sdo(0x2FU, 0x6060U, 0U, 3U));

    for (const std::uint16_t value : std::array<std::uint16_t, 2>{0U, 1U}) {
        CHECK(robot_control_canopen_qualification_authorize_command_application(value));
        expect_sent(sockets, sdo(0x2BU, 0x200FU, 0U, value));
    }
    CHECK(!robot_control_canopen_qualification_authorize_command_application(2U));

    for (const std::uint16_t value : std::array<std::uint16_t, 2>{0U, 500U}) {
        CHECK(robot_control_canopen_qualification_authorize_heartbeat_producer(value));
        expect_sent(sockets, sdo(0x2BU, 0x1017U, 0U, value));
    }
    CHECK(!robot_control_canopen_qualification_authorize_heartbeat_producer(1U));
    CHECK(!robot_control_canopen_qualification_authorize_heartbeat_producer(501U));

    for (const std::uint16_t value : std::array<std::uint16_t, 2>{0U, 1000U}) {
        CHECK(robot_control_canopen_qualification_authorize_watchdog(value));
        expect_sent(sockets, sdo(0x2BU, 0x2000U, 0U, value));
        expect_rejected(sockets[0], sdo(0x2BU, 0x2000U, 0U, value));
    }
    CHECK(!robot_control_canopen_qualification_authorize_watchdog(999U));
    CHECK(!robot_control_canopen_qualification_authorize_watchdog(1001U));
    for (const std::uint16_t value : std::array<std::uint16_t, 2>{0U, 100U}) {
        CHECK(robot_control_canopen_qualification_authorize_tpdo_event_timer(value));
        expect_sent(sockets, sdo(0x2BU, 0x1800U, 5U, value));
    }
    CHECK(!robot_control_canopen_qualification_authorize_tpdo_event_timer(1U));
    CHECK(robot_control_canopen_qualification_authorize_tpdo_event_timer(0U));
    expect_rejected(sockets[0], sdo(0x2BU, 0x1801U, 5U, 0U));
    CHECK(robot_control_canopen_qualification_authorize_watchdog(1000U));
    expect_rejected(sockets[0], sdo(0x23U, 0x2000U, 0U, 1000U));
    CHECK(robot_control_canopen_qualification_upload_size({0x2000U, 0U}) == 2U);
    CHECK(robot_control_canopen_qualification_upload_size({0x2000U, 1U}) == 0U);
    CHECK(robot_control_canopen_qualification_upload_size({0x1800U, 5U}) == 2U);
    CHECK(robot_control_canopen_qualification_upload_size({0x1800U, 1U}) == 4U);
    CHECK(robot_control_canopen_qualification_upload_size({0x1800U, 2U}) == 1U);
    CHECK(robot_control_canopen_qualification_upload_size({0x1A00U, 0U}) == 1U);
    CHECK(robot_control_canopen_qualification_upload_size({0x1A00U, 1U}) == 4U);
    CHECK(robot_control_canopen_qualification_upload_size({0x1A00U, 3U}) == 0U);
    for (const auto request :
         std::array{sdo(0x23U, 0x1800U, 1U, 0x80000181U), sdo(0x23U, 0x1800U, 1U, 0x181U),
                    sdo(0x2FU, 0x1800U, 2U, 255U), sdo(0x2FU, 0x1A00U, 0U, 0U), sdo(0x2FU, 0x1A00U, 0U, 2U),
                    sdo(0x23U, 0x1A00U, 1U, 0x606C0320U), sdo(0x23U, 0x1A00U, 2U, 0x60410020U),
                    sdo(0x23U, 0x1A00U, 1U, 0x60410020U), sdo(0x23U, 0x1A00U, 2U, 0x606C0320U)}) {
        const auto index = static_cast<std::uint16_t>(request.data[1] | request.data[2] << 8U);
        const auto value =
            static_cast<std::uint32_t>(request.data[4]) | static_cast<std::uint32_t>(request.data[5]) << 8U
            | static_cast<std::uint32_t>(request.data[6]) << 16U | static_cast<std::uint32_t>(request.data[7]) << 24U;
        const auto size = static_cast<std::uint8_t>(request.data[0] == 0x2FU ? 1U : 4U);
        CHECK(robot_control_canopen_qualification_authorize_tpdo_mapping({index, request.data[3]}, value, size));
        expect_sent(sockets, request);
        expect_rejected(sockets[0], request);
    }
    CHECK(!robot_control_canopen_qualification_authorize_tpdo_mapping({0x1800U, 1U}, 0x182U, 4U));
    CHECK(!robot_control_canopen_qualification_authorize_tpdo_mapping({0x1801U, 1U}, 0x181U, 4U));
    CHECK(!robot_control_canopen_qualification_authorize_tpdo_mapping({0x1A00U, 1U}, 0x606C0320U, 2U));
    CHECK(!robot_control_canopen_qualification_authorize_tpdo_mapping({0x1A00U, 3U}, 0x606C0320U, 4U));
    CHECK(!robot_control_canopen_qualification_authorize_tpdo_mapping({0x1A00U, 0U}, 3U, 1U));

    for (const std::uint16_t value : std::array<std::uint16_t, 5>{0x0000U, 0x0002U, 0x0006U, 0x0007U, 0x000FU}) {
        CHECK(robot_control_canopen_qualification_authorize_controlword(value));
        expect_sent(sockets, sdo(0x2BU, 0x6040U, 0U, value));
    }
    CHECK(!robot_control_canopen_qualification_authorize_controlword(0x0003U));

    for (const std::uint8_t subindex : std::array<std::uint8_t, 2>{1U, 2U}) {
        for (const std::int32_t rpm : std::array<std::int32_t, 3>{-10, 0, 10}) {
            CHECK(robot_control_canopen_qualification_authorize_target(subindex, rpm));
            expect_sent(sockets, sdo(0x23U, 0x60FFU, subindex, static_cast<std::uint32_t>(rpm)));
        }
    }
    CHECK(!robot_control_canopen_qualification_authorize_target(0U, 0));
    CHECK(!robot_control_canopen_qualification_authorize_target(3U, 0));
    CHECK(!robot_control_canopen_qualification_authorize_target(1U, -11));
    CHECK(!robot_control_canopen_qualification_authorize_target(1U, 11));

    const std::array uploads{robot_control_canopen_qualification_object_t{0x1017U, 0U},
                             robot_control_canopen_qualification_object_t{0x200FU, 0U},
                             robot_control_canopen_qualification_object_t{0x6040U, 0U},
                             robot_control_canopen_qualification_object_t{0x605AU, 0U},
                             robot_control_canopen_qualification_object_t{0x603FU, 0U},
                             robot_control_canopen_qualification_object_t{0x6041U, 0U},
                             robot_control_canopen_qualification_object_t{0x6060U, 0U},
                             robot_control_canopen_qualification_object_t{0x6061U, 0U},
                             robot_control_canopen_qualification_object_t{0x606CU, 1U},
                             robot_control_canopen_qualification_object_t{0x606CU, 2U},
                             robot_control_canopen_qualification_object_t{0x606CU, 3U},
                             robot_control_canopen_qualification_object_t{0x60FFU, 1U},
                             robot_control_canopen_qualification_object_t{0x60FFU, 2U}};
    for (const auto object : uploads) {
        CHECK(robot_control_canopen_qualification_upload_size(object) != 0U);
        CHECK(robot_control_canopen_qualification_authorize_upload(object));
        expect_sent(sockets, sdo(0x40U, object.index, object.subindex));
    }
    CHECK(robot_control_canopen_qualification_upload_size({0x2010U, 0U}) == 0U);
    CHECK(robot_control_canopen_qualification_upload_size({0x200FU, 1U}) == 0U);
    CHECK(robot_control_canopen_qualification_upload_size({0x60FFU, 3U}) == 4U);
    for (const std::uint32_t value : {0U, 5U, 5U << 16U}) {
        const std::uint16_t controlword = value == 0U ? 6U : 15U;
        CHECK(robot_control_canopen_qualification_authorize_rpdo(controlword, value));
        can_frame request{};
        request.can_id = 0x201U;
        request.can_dlc = 6U;
        request.data[0] = static_cast<std::uint8_t>(controlword);
        for (unsigned i = 0U; i < 4U; ++i) {
            request.data[i + 2U] = static_cast<std::uint8_t>(value >> (8U * i));
        }
        expect_sent(sockets, request);
        expect_rejected(sockets[0], request);
        CHECK(robot_control_canopen_qualification_authorize_rpdo(controlword, value));
        request.can_id = 0x301U;
        expect_rejected(sockets[0], request);
    }
    CHECK(!robot_control_canopen_qualification_authorize_rpdo(15U, 0U));
    CHECK(!robot_control_canopen_qualification_authorize_rpdo(6U, 5U));
    CHECK(!robot_control_canopen_qualification_authorize_rpdo(128U, 0U));
    CHECK(!robot_control_canopen_qualification_authorize_rpdo(15U, 0x00050005U));
    CHECK(!robot_control_canopen_qualification_authorize_rpdo_mapping({0x1401U, 1U}, 0x201U, 4U));
    CHECK(!robot_control_canopen_qualification_authorize_rpdo_mapping({0x1600U, 0U}, 3U, 1U));
    CHECK(!robot_control_canopen_qualification_authorize_rpdo_mapping({0x1600U, 2U}, 0x60FF0120U, 4U));
    for (const std::uint32_t value : {0U, 10U, 65526U, 10U << 16U, 65526U << 16U}) {
        CHECK(robot_control_canopen_qualification_authorize_packed_target(value));
        expect_sent(sockets, sdo(0x23U, 0x60FFU, 3U, value));
        expect_rejected(sockets[0], sdo(0x23U, 0x60FFU, 3U, value));
    }
    for (const std::uint32_t value : {11U, 65525U, 11U << 16U, 65525U << 16U, 0x00050005U, 0xFFFF0005U}) {
        CHECK(!robot_control_canopen_qualification_authorize_packed_target(value));
    }

    CHECK(robot_control_canopen_qualification_authorize_velocity_mode());
    auto wrong = sdo(0x2FU, 0x6060U, 0U, 3U);
    wrong.can_id = 0x602U;
    expect_rejected(sockets[0], wrong);
    CHECK(robot_control_canopen_qualification_authorize_velocity_mode());
    wrong = sdo(0x2BU, 0x6060U, 0U, 3U);
    expect_rejected(sockets[0], wrong);
    CHECK(robot_control_canopen_qualification_authorize_velocity_mode());
    wrong = sdo(0x2FU, 0x6060U, 0U, 3U);
    wrong.can_dlc = 7U;
    expect_rejected(sockets[0], wrong);
    CHECK(robot_control_canopen_qualification_authorize_velocity_mode());
    wrong = sdo(0x2FU, 0x6060U, 0U, 3U);
    wrong.data[3] = 1U;
    expect_rejected(sockets[0], wrong);
    CHECK(robot_control_canopen_qualification_authorize_velocity_mode());
    wrong = sdo(0x2FU, 0x6060U, 0U, 3U);
    wrong.data[4] = 4U;
    expect_rejected(sockets[0], wrong);
    CHECK(robot_control_canopen_qualification_authorize_velocity_mode());
    wrong = sdo(0x2FU, 0x6060U, 0U, 3U);
    wrong.can_id = 0x201U;
    expect_rejected(sockets[0], wrong);
    CHECK(robot_control_canopen_qualification_authorize_nmt(0x01U));
    can_frame broadcast{};
    broadcast.can_dlc = 2U;
    broadcast.data[0] = 0x01U;
    expect_rejected(sockets[0], broadcast);

    CHECK(::close(sockets[0]) == 0);
    CHECK(::close(sockets[1]) == 0);
    return failures == 0 ? 0 : 1;
}
