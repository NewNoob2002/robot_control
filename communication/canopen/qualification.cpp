#include "communication/canopen/qualification.hpp"

#include "communication/canopen/qualification_gate.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cerrno>
#include <chrono>
#include <iostream>
#include <limits>
#include <linux/can.h>
#include <string>
#include <sys/socket.h>

namespace robot_control::communication::canopen {
namespace {

using namespace std::chrono_literals;
using domain::drive::zlac8015d::IndependentChannel;
using domain::drive::zlac8015d::TransitionControlword;

constexpr std::int32_t qualification_target_limit_rpm = 10;
constexpr auto qualification_duration_limit = 3s;
constexpr auto qualification_shutdown_duration_limit = 10s;
constexpr auto qualification_supervision_step = 10ms;

/** Increment one generation while reserving zero as invalid. */
void increment_nonzero(std::uint64_t& value) noexcept {
    ++value;
    if (value == 0U) {
        ++value;
    }
}

/** Convert one qualification failure into the project status contract. */
platform::linux::Status failure(const char* operation, const StackConfig& config, const int error_number) {
    return platform::linux::Status::from_errno(
        operation, "interface=" + config.interface_name + " remote=" + std::to_string(config.remote_node_id),
        error_number);
}

/** Return the neutral independent object subindex. */
std::uint8_t subindex(const IndependentChannel channel) noexcept {
    return static_cast<std::uint8_t>(channel);
}

/** Load one little-endian unsigned value from one through four bytes. */
std::uint32_t load_u32(const std::span<const std::byte> data) noexcept {
    std::uint32_t value = 0U;
    for (std::size_t index = 0U; index < data.size(); ++index) {
        value |= static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(data[index]))
                 << static_cast<unsigned int>(index * 8U);
    }
    return value;
}

/** Bound one SDO transaction by both its configured timeout and an operation deadline. */
std::chrono::milliseconds sdo_timeout_until(const std::chrono::milliseconds configured,
                                            const std::chrono::steady_clock::time_point deadline) noexcept {
    if (deadline == std::chrono::steady_clock::time_point::max()) {
        return configured;
    }
    const auto now = std::chrono::steady_clock::now();
    return now >= deadline ? 0ms : std::min(configured, std::chrono::ceil<std::chrono::milliseconds>(deadline - now));
}

} // namespace

QualificationSession::QualificationSession(Lifecycle& lifecycle) noexcept : lifecycle_{&lifecycle} {}

QualificationState QualificationSession::state() const noexcept {
    return state_;
}

platform::linux::Status QualificationSession::require_clean_generation() const noexcept {
    if (lifecycle_->storage_->config().remote_node_id != 1U) {
        return failure("qualification_precondition", lifecycle_->storage_->config(), EACCES);
    }
    const auto snapshot = lifecycle_->observation_snapshot(std::chrono::steady_clock::now());
    if (sequence_generation_.transport != 0U
        && (snapshot.generation != sequence_generation_ || snapshot.emergency.frame.present
            || snapshot.can_error.present || snapshot.malformed_count != 0U)) {
        return failure("qualification_sequence_generation_or_bus_error", lifecycle_->storage_->config(), EIO);
    }
    return platform::linux::Status::success();
}

platform::linux::Status QualificationSession::require_fresh_remote() const noexcept {
    const auto clean = require_clean_generation();
    if (!clean.ok()) {
        return clean;
    }
    const auto snapshot = lifecycle_->observation_snapshot(std::chrono::steady_clock::now());
    return snapshot.heartbeat.frame.current
               ? platform::linux::Status::success()
               : failure("qualification_precondition", lifecycle_->storage_->config(), ENOTCONN);
}

platform::linux::Status QualificationSession::require_ready() const noexcept {
    if (state_ != QualificationState::ready) {
        return failure("qualification_inhibited", lifecycle_->storage_->config(), EACCES);
    }
    return require_fresh_remote();
}

platform::linux::Status QualificationSession::require_motion_feedback() const noexcept {
    const auto ready = require_fresh_remote();
    if (!ready.ok()) {
        return ready;
    }
    const auto snapshot = lifecycle_->observation_snapshot(std::chrono::steady_clock::now());
    if (!snapshot.heartbeat.frame.current || !snapshot.tpdo[0].current || snapshot.emergency.frame.current
        || snapshot.can_error.current) {
        return failure("qualification_motion_feedback", lifecycle_->storage_->config(), ENODATA);
    }
    return platform::linux::Status::success();
}

platform::linux::Status
QualificationSession::require_operation_enabled_feedback(const IndependentChannel channel) const noexcept {
    const auto ready = require_motion_feedback();
    if (!ready.ok()) {
        return ready;
    }
    const auto snapshot = lifecycle_->observation_snapshot(std::chrono::steady_clock::now());
    const auto& bytes = snapshot.tpdo[0].raw.payload;
    const auto status = domain::drive::decode_dual_axis_status(
        static_cast<std::uint32_t>(bytes[0]) | (static_cast<std::uint32_t>(bytes[1]) << 8U)
        | (static_cast<std::uint32_t>(bytes[2]) << 16U) | (static_cast<std::uint32_t>(bytes[3]) << 24U));
    if (status.low_half.state != domain::drive::Cia402State::operation_enabled
        || status.high_half.state != domain::drive::Cia402State::operation_enabled) {
        return failure("qualification_motion_not_enabled", lifecycle_->storage_->config(), EPROTO);
    }
    const std::uint16_t low_raw =
        static_cast<std::uint16_t>(bytes[4]) | static_cast<std::uint16_t>(static_cast<std::uint16_t>(bytes[5]) << 8U);
    const std::uint16_t high_raw =
        static_cast<std::uint16_t>(bytes[6]) | static_cast<std::uint16_t>(static_cast<std::uint16_t>(bytes[7]) << 8U);
    const auto low = std::bit_cast<std::int16_t>(low_raw);
    const auto high = std::bit_cast<std::int16_t>(high_raw);
    const auto other = channel == IndependentChannel::subindex_1 ? high : low;
    return other == 0 ? platform::linux::Status::success()
                      : failure("qualification_other_channel_velocity", lifecycle_->storage_->config(), ERANGE);
}

platform::linux::Status QualificationSession::inhibit(platform::linux::Status status) noexcept {
    robot_control_canopen_qualification_clear_authorization();
    state_ = QualificationState::cleanup_required;
    return status;
}

platform::linux::Status QualificationSession::download(const std::uint16_t index, const std::uint8_t object_subindex,
                                                       const std::span<const std::byte> data, const bool require_fresh,
                                                       const std::chrono::steady_clock::time_point deadline) noexcept {
    if (require_fresh) {
        auto ready = require_fresh_remote();
        if (!ready.ok()) {
            return ready;
        }
    }
    if (data.empty() || data.size() > 4U) {
        return failure("qualification_download_width", lifecycle_->storage_->config(), EINVAL);
    }
    const auto sdo_timeout = sdo_timeout_until(lifecycle_->storage_->config().sdo_timeout, deadline);
    if (sdo_timeout <= 0ms) {
        return failure("qualification_sdo_deadline", lifecycle_->storage_->config(), ETIMEDOUT);
    }

    bool authorized = false;
    const std::uint32_t value = load_u32(data);
    if (index == 0x6060U && object_subindex == 0U && data.size() == 1U && value == 3U) {
        authorized = robot_control_canopen_qualification_authorize_velocity_mode();
    } else if (index == 0x1017U && object_subindex == 0U && data.size() == 2U) {
        authorized =
            robot_control_canopen_qualification_authorize_heartbeat_producer(static_cast<std::uint16_t>(value));
    } else if (index == 0x200FU && object_subindex == 0U && data.size() == 2U) {
        authorized =
            robot_control_canopen_qualification_authorize_command_application(static_cast<std::uint16_t>(value));
    } else if (index == 0x2000U && object_subindex == 0U && data.size() == 2U) {
        authorized = robot_control_canopen_qualification_authorize_watchdog(static_cast<std::uint16_t>(value));
    } else if (index == 0x1800U && object_subindex == 5U && data.size() == 2U) {
        authorized = robot_control_canopen_qualification_authorize_tpdo_event_timer(static_cast<std::uint16_t>(value));
    } else if (index == 0x1400U || index == 0x1600U) {
        authorized = robot_control_canopen_qualification_authorize_rpdo_mapping(
            {index, object_subindex}, value, static_cast<std::uint8_t>(data.size()));
    } else if (index == 0x1800U || index == 0x1A00U) {
        authorized = robot_control_canopen_qualification_authorize_tpdo_mapping({index, object_subindex}, value,
                                                                                static_cast<std::uint8_t>(data.size()));
    } else if (index == 0x6040U && object_subindex == 0U && data.size() == 2U) {
        authorized = robot_control_canopen_qualification_authorize_controlword(static_cast<std::uint16_t>(value));
    } else if (index == 0x60FFU && data.size() == 4U) {
        authorized = object_subindex == 3U
                         ? robot_control_canopen_qualification_authorize_packed_target(value)
                         : robot_control_canopen_qualification_authorize_target(object_subindex,
                                                                                 std::bit_cast<std::int32_t>(value));
    }
    if (!authorized) {
        return failure("qualification_authorize_download", lifecycle_->storage_->config(), EACCES);
    }

    CO_SDOclient_t* const client = lifecycle_->storage_->stack()->SDOclient;
    const auto initiated = CO_SDOclientDownloadInitiate(client, index, object_subindex, data.size(),
                                                        static_cast<std::uint16_t>(sdo_timeout.count()), false);
    std::array<std::uint8_t, 4> bytes{};
    std::transform(data.begin(), data.end(), bytes.begin(), [](const std::byte byte) {
        return std::to_integer<std::uint8_t>(byte);
    });
    if (initiated != CO_SDO_RT_ok_communicationEnd
        || CO_SDOclientDownloadBufWrite(client, bytes.data(), data.size()) != data.size()) {
        CO_SDOclientClose(client);
        robot_control_canopen_qualification_clear_authorization();
        return failure("CO_SDOclientDownloadInitiate", lifecycle_->storage_->config(), EPROTO);
    }

    CO_SDO_abortCode_t abort_code = CO_SDO_AB_NONE;
    auto previous = std::chrono::steady_clock::now();
    CO_SDO_return_t result = CO_SDOclientDownload(client, 0U, false, false, &abort_code, nullptr, nullptr);
    while (result > CO_SDO_RT_ok_communicationEnd) {
        const auto run = lifecycle_->run_until(std::min(deadline, std::chrono::steady_clock::now() + 1ms));
        if (!run.ok() || run.value() != LifecycleExit::deadline) {
            CO_SDOclientClose(client);
            robot_control_canopen_qualification_clear_authorization();
            return run.ok() ? failure("qualification_owner_exit", lifecycle_->storage_->config(), ECANCELED)
                            : run.status();
        }
        if (require_fresh && sequence_generation_.transport != 0U) {
            const auto supervised = require_fresh_remote();
            if (!supervised.ok()) {
                lifecycle_->observations_.cancel_sdo_upload();
                CO_SDOclientClose(client);
                robot_control_canopen_qualification_clear_authorization();
                return supervised;
            }
        }
        const auto now = std::chrono::steady_clock::now();
        const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(now - previous).count();
        previous = now;
        result =
            CO_SDOclientDownload(client,
                                 static_cast<std::uint32_t>(std::clamp<std::int64_t>(
                                     elapsed, 0, static_cast<std::int64_t>(std::numeric_limits<std::uint32_t>::max()))),
                                 false, false, &abort_code, nullptr, nullptr);
    }
    robot_control_canopen_qualification_clear_authorization();
    CO_SDOclientClose(client);
    if (result == CO_SDO_RT_ok_communicationEnd) {
        return platform::linux::Status::success();
    }
    return failure("CO_SDOclientDownload", lifecycle_->storage_->config(),
                   abort_code == CO_SDO_AB_TIMEOUT ? ETIMEDOUT : EPROTO);
}

platform::linux::Result<SdoObservation>
QualificationSession::upload(const std::uint16_t index, const std::uint8_t object_subindex, const bool require_fresh,
                             const std::chrono::steady_clock::time_point deadline) noexcept {
    const robot_control_canopen_qualification_object_t object{.index = index, .subindex = object_subindex};
    const std::uint8_t expected_size = robot_control_canopen_qualification_upload_size(object);
    if (require_fresh) {
        auto ready = require_fresh_remote();
        if (!ready.ok()) {
            return platform::linux::Result<SdoObservation>::failure(ready);
        }
    }
    if (expected_size == 0U) {
        return platform::linux::Result<SdoObservation>::failure(
            failure("qualification_authorize_upload", lifecycle_->storage_->config(), EACCES));
    }
    const auto sdo_timeout = sdo_timeout_until(lifecycle_->storage_->config().sdo_timeout, deadline);
    if (sdo_timeout <= 0ms) {
        return platform::linux::Result<SdoObservation>::failure(
            failure("qualification_sdo_deadline", lifecycle_->storage_->config(), ETIMEDOUT));
    }

    increment_nonzero(request_generation_);
    increment_nonzero(attempt_generation_);
    lifecycle_->observations_.begin_sdo_upload(request_generation_, attempt_generation_, index, object_subindex,
                                               expected_size);
    CO_SDOclient_t* const client = lifecycle_->storage_->stack()->SDOclient;
    if (CO_SDOclientUploadInitiate(client, index, object_subindex, static_cast<std::uint16_t>(sdo_timeout.count()),
                                   false)
            != CO_SDO_RT_ok_communicationEnd
        || !robot_control_canopen_qualification_authorize_upload(object)) {
        lifecycle_->observations_.cancel_sdo_upload();
        CO_SDOclientClose(client);
        robot_control_canopen_qualification_clear_authorization();
        return platform::linux::Result<SdoObservation>::failure(
            failure("CO_SDOclientUploadInitiate", lifecycle_->storage_->config(), EPROTO));
    }

    CO_SDO_abortCode_t abort_code = CO_SDO_AB_NONE;
    auto previous = std::chrono::steady_clock::now();
    CO_SDO_return_t result = CO_SDOclientUpload(client, 0U, false, &abort_code, nullptr, nullptr, nullptr);
    while (result > CO_SDO_RT_ok_communicationEnd) {
        const auto run = lifecycle_->run_until(std::min(deadline, std::chrono::steady_clock::now() + 1ms));
        if (!run.ok() || run.value() != LifecycleExit::deadline) {
            lifecycle_->observations_.cancel_sdo_upload();
            CO_SDOclientClose(client);
            robot_control_canopen_qualification_clear_authorization();
            return platform::linux::Result<SdoObservation>::failure(
                run.ok() ? failure("qualification_owner_exit", lifecycle_->storage_->config(), ECANCELED)
                         : run.status());
        }
        if (require_fresh && sequence_generation_.transport != 0U) {
            const auto supervised = require_fresh_remote();
            if (!supervised.ok()) {
                lifecycle_->observations_.cancel_sdo_upload();
                CO_SDOclientClose(client);
                robot_control_canopen_qualification_clear_authorization();
                return platform::linux::Result<SdoObservation>::failure(supervised);
            }
        }
        const auto now = std::chrono::steady_clock::now();
        const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(now - previous).count();
        previous = now;
        result =
            CO_SDOclientUpload(client,
                               static_cast<std::uint32_t>(std::clamp<std::int64_t>(
                                   elapsed, 0, static_cast<std::int64_t>(std::numeric_limits<std::uint32_t>::max()))),
                               false, &abort_code, nullptr, nullptr, nullptr);
    }
    robot_control_canopen_qualification_clear_authorization();
    const auto snapshot = lifecycle_->observation_snapshot(std::chrono::steady_clock::now());
    CO_SDOclientClose(client);
    if (result != CO_SDO_RT_ok_communicationEnd || !snapshot.sdo_result.frame.current
        || snapshot.sdo_result.outcome != SdoOutcome::expedited_upload
        || snapshot.sdo_result.request_generation != request_generation_
        || snapshot.sdo_result.attempt_generation != attempt_generation_) {
        lifecycle_->observations_.cancel_sdo_upload();
        return platform::linux::Result<SdoObservation>::failure(
            failure("qualification_upload", lifecycle_->storage_->config(),
                    abort_code == CO_SDO_AB_TIMEOUT ? ETIMEDOUT : EPROTO));
    }
    return platform::linux::Result<SdoObservation>::success(snapshot.sdo_result);
}

platform::linux::Status QualificationSession::verify_download(
    const std::uint16_t index, const std::uint8_t object_subindex, const std::span<const std::byte> data,
    const std::uint16_t readback_index, const std::uint8_t readback_subindex, const std::span<const std::byte> expected,
    const bool require_fresh, const std::chrono::steady_clock::time_point deadline) noexcept {
    auto status = download(index, object_subindex, data, require_fresh, deadline);
    if (!status.ok()) {
        return status;
    }
    const auto readback = upload(readback_index, readback_subindex, require_fresh, deadline);
    if (!readback.ok()) {
        return readback.status();
    }
    if (readback.value().data_length != expected.size()) {
        return failure("qualification_readback", lifecycle_->storage_->config(), EPROTO);
    }
    for (std::size_t index_value = 0U; index_value < expected.size(); ++index_value) {
        if (readback.value().data[index_value] != std::to_integer<std::uint8_t>(expected[index_value])) {
            return failure("qualification_readback", lifecycle_->storage_->config(), EPROTO);
        }
    }
    return platform::linux::Status::success();
}

platform::linux::Status
QualificationSession::verify_target(const IndependentChannel channel, const std::int32_t rpm, const bool require_fresh,
                                    const std::chrono::steady_clock::time_point deadline) noexcept {
    const auto encoded = domain::drive::zlac8015d::encode_independent_target(channel, rpm);
    if (!encoded || rpm < -qualification_target_limit_rpm || rpm > qualification_target_limit_rpm) {
        return failure("qualification_target_range", lifecycle_->storage_->config(), ERANGE);
    }
    if (synchronous_targets_) {
        std::array<std::byte, 4> packed{};
        const auto offset = channel == IndependentChannel::subindex_1 ? 0U : 2U;
        packed[offset] = (*encoded)[0];
        packed[offset + 1U] = (*encoded)[1];
        if (rpdo_interval_) {
            if (std::chrono::steady_clock::now() >= deadline) {
                return failure("qualification_rpdo_deadline", lifecycle_->storage_->config(), ETIMEDOUT);
            }
            const auto sent = send_rpdo_target(load_u32(packed));
            if (!sent.ok()) {
                return sent;
            }
            const auto readback = upload(0x60FFU, 3U, require_fresh, deadline);
            if (!readback.ok()) {
                return readback.status();
            }
            return load_u32(packed) == (static_cast<std::uint32_t>(readback.value().data[0])
                       | (static_cast<std::uint32_t>(readback.value().data[1]) << 8U)
                       | (static_cast<std::uint32_t>(readback.value().data[2]) << 16U)
                       | (static_cast<std::uint32_t>(readback.value().data[3]) << 24U))
                       ? platform::linux::Status::success()
                       : failure("qualification_rpdo_readback", lifecycle_->storage_->config(), EPROTO);
        }
        return verify_download(0x60FFU, 3U, packed, 0x60FFU, 3U, packed, require_fresh, deadline);
    }
    return verify_download(0x60FFU, subindex(channel), *encoded, 0x60FFU, subindex(channel), *encoded, require_fresh,
                           deadline);
}

platform::linux::Status QualificationSession::preflight_rpdo_mapping() noexcept {
    struct Entry { std::uint16_t index; std::uint8_t sub; std::uint32_t value; };
    constexpr std::array entries{Entry{0x1400U, 1U, 0x201U}, Entry{0x1400U, 2U, 255U},
        Entry{0x1400U, 5U, 1000U}, Entry{0x1600U, 0U, 2U},
        Entry{0x1600U, 1U, 0x60400010U}, Entry{0x1600U, 2U, 0x60600008U}};
    for (const auto entry : entries) {
        const auto readback = upload(entry.index, entry.sub, true);
        if (!readback.ok()) {
            return readback.status();
        }
        const auto& bytes = readback.value().data;
        const auto value = static_cast<std::uint32_t>(bytes[0]) | (static_cast<std::uint32_t>(bytes[1]) << 8U)
            | (static_cast<std::uint32_t>(bytes[2]) << 16U) | (static_cast<std::uint32_t>(bytes[3]) << 24U);
        if (value != entry.value) {
            return failure("qualification_rpdo_baseline", lifecycle_->storage_->config(), EPROTO);
        }
    }
    return platform::linux::Status::success();
}

platform::linux::Status QualificationSession::configure_rpdo_mapping(const bool restore) noexcept {
    const auto snapshot = lifecycle_->observation_snapshot(std::chrono::steady_clock::now());
    if (!snapshot.nmt.current || snapshot.nmt.state != RemoteNmtState::pre_operational) {
        return failure("qualification_rpdo_not_preoperational", lifecycle_->storage_->config(), EACCES);
    }
    struct Entry { std::uint16_t index; std::uint8_t sub; std::uint32_t value; std::uint8_t size; };
    const std::array entries{Entry{0x1400U, 1U, 0x80000201U, 4U}, Entry{0x1600U, 0U, 0U, 1U},
        Entry{0x1600U, 1U, 0x60400010U, 4U},
        Entry{0x1600U, 2U, restore ? 0x60600008U : 0x60FF0320U, 4U},
        Entry{0x1600U, 0U, 2U, 1U}, Entry{0x1400U, 1U, 0x201U, 4U}};
    for (const auto entry : entries) {
        const std::array bytes{std::byte{static_cast<std::uint8_t>(entry.value)},
            std::byte{static_cast<std::uint8_t>(entry.value >> 8U)},
            std::byte{static_cast<std::uint8_t>(entry.value >> 16U)},
            std::byte{static_cast<std::uint8_t>(entry.value >> 24U)}};
        const auto data = std::span{bytes}.first(entry.size);
        const auto status = verify_download(entry.index, entry.sub, data, entry.index, entry.sub, data, false);
        if (!status.ok()) {
            return status;
        }
    }
    return platform::linux::Status::success();
}

platform::linux::Status QualificationSession::send_rpdo_target(const std::uint32_t packed) noexcept {
    const auto* const module = lifecycle_->storage_->stack()->CANmodule;
    if (!rpdo_mapping_owned_ || !rpdo_interval_ || module->CANinterfaceCount != 1U
        || module->CANinterfaces[0].fd < 0) {
        return failure("qualification_rpdo_unavailable", lifecycle_->storage_->config(), EACCES);
    }
    const std::uint16_t controlword = packed == 0U ? 6U : 15U;
    if (!robot_control_canopen_qualification_authorize_rpdo(controlword, packed)) {
        return failure("qualification_rpdo_authorization", lifecycle_->storage_->config(), EACCES);
    }
    can_frame frame{};
    frame.can_id = 0x201U;
    frame.can_dlc = 6U;
    frame.data[0] = static_cast<std::uint8_t>(controlword);
    for (unsigned i = 0U; i < 4U; ++i) {
        frame.data[2U + i] = static_cast<std::uint8_t>(packed >> (8U * i));
    }
    const auto sent = robot_control_canopen_qualification_transmit(
        module->CANinterfaces[0].fd, &frame, CAN_MTU, MSG_DONTWAIT);
    const auto error = errno;
    robot_control_canopen_qualification_clear_authorization();
    return sent == CAN_MTU ? platform::linux::Status::success()
                          : failure("qualification_rpdo_send", lifecycle_->storage_->config(), error == 0 ? EIO : error);
}

platform::linux::Status QualificationSession::set_heartbeat_producer_raw(const std::uint16_t milliseconds,
                                                                         const bool require_fresh) noexcept {
    const std::array data{std::byte{static_cast<std::uint8_t>(milliseconds)},
                          std::byte{static_cast<std::uint8_t>(milliseconds >> 8U)}};
    return verify_download(0x1017U, 0U, data, 0x1017U, 0U, data, require_fresh);
}

platform::linux::Status QualificationSession::start_online_heartbeat(const std::chrono::milliseconds timeout) noexcept {
    auto status = require_clean_generation();
    if (!status.ok()) {
        return status;
    }
    const auto heartbeat = upload(0x1017U, 0U, false);
    if (!heartbeat.ok()) {
        return heartbeat.status();
    }
    if (heartbeat.value().data != std::array<std::uint8_t, 4>{}) {
        return failure("qualification_heartbeat_producer_baseline", lifecycle_->storage_->config(), EPROTO);
    }
    heartbeat_restore_required_ = true;
    const auto started_at = std::chrono::steady_clock::now();
    status = set_heartbeat_producer_raw(500U, false);
    return status.ok() ? wait_heartbeat(started_at, timeout) : status;
}

platform::linux::Status QualificationSession::finish_online_sequence(platform::linux::Status status) noexcept {
    const auto restored = restore_heartbeat_producer();
    if (!restored.ok()) {
        if (status.ok()) {
            status = restored;
        } else {
            status.context += " heartbeat_restore_failed=" + restored.operation + ":" + restored.error.message();
        }
    }
    return inhibit(std::move(status));
}

platform::linux::Status QualificationSession::restore_heartbeat_producer() noexcept {
    if (!heartbeat_restore_required_) {
        return platform::linux::Status::success();
    }
    const auto status = set_heartbeat_producer_raw(0U, false);
    if (status.ok()) {
        heartbeat_restore_required_ = false;
    }
    return status;
}

platform::linux::Status QualificationSession::send_nmt_raw(const QualificationNmt command,
                                                           const bool require_fresh) noexcept {
    if (require_fresh) {
        auto ready = require_fresh_remote();
        if (!ready.ok()) {
            return ready;
        }
    }
    if (!robot_control_canopen_qualification_authorize_nmt(static_cast<std::uint8_t>(command))) {
        return failure("qualification_authorize_nmt", lifecycle_->storage_->config(), EACCES);
    }
    const auto result = CO_NMT_sendCommand(lifecycle_->storage_->stack()->NMT, static_cast<CO_NMT_command_t>(command),
                                           lifecycle_->storage_->config().remote_node_id);
    robot_control_canopen_qualification_clear_authorization();
    return result == CO_ERROR_NO ? platform::linux::Status::success()
                                 : failure("CO_NMT_sendCommand", lifecycle_->storage_->config(), EIO);
}

platform::linux::Status QualificationSession::send_controlword_raw(const TransitionControlword controlword,
                                                                   const bool require_fresh) noexcept {
    const auto encoded = domain::drive::zlac8015d::encode_transition_controlword(controlword);
    if (!encoded) {
        return failure("qualification_controlword", lifecycle_->storage_->config(), EINVAL);
    }
    return download(0x6040U, 0U, *encoded, require_fresh);
}

platform::linux::Status QualificationSession::send_nmt(const QualificationNmt command) noexcept {
    if (command != QualificationNmt::stopped && command != QualificationNmt::pre_operational) {
        return inhibit(failure("qualification_standalone_activation", lifecycle_->storage_->config(), EACCES));
    }
    auto ready = require_ready();
    if (!ready.ok()) {
        return inhibit(ready);
    }
    auto status = send_nmt_raw(command, true);
    return status.ok() ? status : inhibit(status);
}

platform::linux::Status QualificationSession::set_velocity_mode() noexcept {
    if (sequence_generation_.transport == 0U) {
        return inhibit(failure("qualification_standalone_activation", lifecycle_->storage_->config(), EACCES));
    }
    auto ready = require_ready();
    if (!ready.ok()) {
        return inhibit(ready);
    }
    const std::array value{std::byte{3U}};
    auto status = verify_download(0x6060U, 0U, value, 0x6061U, 0U, value, true);
    return status.ok() ? status : inhibit(status);
}

platform::linux::Status QualificationSession::send_controlword(const TransitionControlword controlword) noexcept {
    if (controlword != TransitionControlword::shutdown && controlword != TransitionControlword::disable_voltage
        && controlword != TransitionControlword::quick_stop) {
        return inhibit(failure("qualification_standalone_activation", lifecycle_->storage_->config(), EACCES));
    }
    auto ready = require_ready();
    if (!ready.ok()) {
        return inhibit(ready);
    }
    auto status = send_controlword_raw(controlword, true);
    return status.ok() ? status : inhibit(status);
}

platform::linux::Status QualificationSession::set_zero_targets() noexcept {
    auto status = verify_target(IndependentChannel::subindex_1, 0, false);
    const auto second = verify_target(IndependentChannel::subindex_2, 0, false);
    if (status.ok()) {
        status = second;
    } else if (!second.ok()) {
        status.context += " second_zero_failed=" + second.operation + ":" + second.error.message();
    }
    return status.ok() ? status : inhibit(status);
}

platform::linux::Status
QualificationSession::require_zero_velocity_feedback(const bool require_fresh,
                                                     const std::chrono::steady_clock::time_point deadline) noexcept {
    for (const std::uint8_t part : std::array<std::uint8_t, 3>{1U, 2U, 3U}) {
        const auto value = upload(0x606CU, part, require_fresh, deadline);
        if (!value.ok()) {
            return value.status();
        }
        if (value.value().data != std::array<std::uint8_t, 4>{}) {
            return failure("qualification_nonzero_velocity", lifecycle_->storage_->config(), ERANGE);
        }
    }
    return platform::linux::Status::success();
}

platform::linux::Status QualificationSession::wait_nmt_state(const RemoteNmtState expected,
                                                             const std::chrono::steady_clock::time_point previous,
                                                             const std::chrono::milliseconds timeout) noexcept {
    const auto deadline = previous + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        const auto ready = require_fresh_remote();
        if (!ready.ok()) {
            return ready;
        }
        const auto snapshot = lifecycle_->observation_snapshot(std::chrono::steady_clock::now());
        if (snapshot.tpdo[0].current) {
            const auto& bytes = snapshot.tpdo[0].raw.payload;
            if (std::any_of(bytes.begin() + 4, bytes.end(), [](const auto value) {
                    return value != 0U;
                })) {
                return failure("qualification_nonzero_tpdo_velocity", lifecycle_->storage_->config(), ERANGE);
            }
        }
        if (snapshot.nmt.current && snapshot.nmt.state == expected && snapshot.nmt.raw.received_at > previous) {
            return platform::linux::Status::success();
        }
        const auto run = lifecycle_->run_until(std::min(deadline, std::chrono::steady_clock::now() + 1ms));
        if (!run.ok() || run.value() != LifecycleExit::deadline) {
            return run.ok() ? failure("qualification_owner_exit", lifecycle_->storage_->config(), ECANCELED)
                            : run.status();
        }
    }
    return failure("qualification_nmt_timeout", lifecycle_->storage_->config(), ETIMEDOUT);
}

platform::linux::Status QualificationSession::wait_heartbeat(const std::chrono::steady_clock::time_point previous,
                                                             const std::chrono::milliseconds timeout) noexcept {
    const auto deadline = previous + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        const auto clean = require_clean_generation();
        if (!clean.ok()) {
            return clean;
        }
        const auto snapshot = lifecycle_->observation_snapshot(std::chrono::steady_clock::now());
        if (snapshot.heartbeat.frame.current && snapshot.heartbeat.frame.raw.received_at > previous) {
            return platform::linux::Status::success();
        }
        const auto run = lifecycle_->run_until(std::min(deadline, std::chrono::steady_clock::now() + 1ms));
        if (!run.ok() || run.value() != LifecycleExit::deadline) {
            return run.ok() ? failure("qualification_owner_exit", lifecycle_->storage_->config(), ECANCELED)
                            : run.status();
        }
    }
    return failure("qualification_heartbeat_timeout", lifecycle_->storage_->config(), ETIMEDOUT);
}

platform::linux::Status QualificationSession::wait_dual_state(const domain::drive::Cia402State expected,
                                                              const std::chrono::steady_clock::time_point previous,
                                                              const std::chrono::milliseconds timeout,
                                                              const bool allow_deceleration) noexcept {
    domain::drive::TransitionTracker low;
    domain::drive::TransitionTracker high;
    low.start(expected, 0U, previous, timeout);
    high.start(expected, 0U, previous, timeout);
    const auto deadline = previous + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        const auto ready = require_fresh_remote();
        if (!ready.ok()) {
            return ready;
        }
        const auto now = std::chrono::steady_clock::now();
        const auto snapshot = lifecycle_->observation_snapshot(now);
        const auto& tpdo = snapshot.tpdo[0];
        if (tpdo.current && tpdo.raw.received_at > previous) {
            const auto& bytes = tpdo.raw.payload;
            const bool moving = std::any_of(bytes.begin() + 4, bytes.end(), [](const auto value) {
                return value != 0U;
            });
            if (!allow_deceleration && moving) {
                return failure("qualification_nonzero_tpdo_velocity", lifecycle_->storage_->config(), ERANGE);
            }
            const auto status = domain::drive::decode_dual_axis_status(
                static_cast<std::uint32_t>(bytes[0]) | (static_cast<std::uint32_t>(bytes[1]) << 8U)
                | (static_cast<std::uint32_t>(bytes[2]) << 16U) | (static_cast<std::uint32_t>(bytes[3]) << 24U));
            // Evaluate both halves from this same newer frame; never latch one half across frames.
            low.start(expected, 0U, previous, timeout);
            high.start(expected, 0U, previous, timeout);
            if (!moving && low.evaluate(status.low_half.state, 1U, now) == domain::drive::TransitionResult::reached
                && high.evaluate(status.high_half.state, 1U, now) == domain::drive::TransitionResult::reached) {
                return platform::linux::Status::success();
            }
            if (status.low_half.state == domain::drive::Cia402State::fault
                || status.high_half.state == domain::drive::Cia402State::fault) {
                return failure("qualification_drive_fault", lifecycle_->storage_->config(), EIO);
            }
        }
        const auto run = lifecycle_->run_until(std::min(deadline, now + 1ms));
        if (!run.ok() || run.value() != LifecycleExit::deadline) {
            return run.ok() ? failure("qualification_owner_exit", lifecycle_->storage_->config(), ECANCELED)
                            : run.status();
        }
    }
    return failure("qualification_dual_state_timeout", lifecycle_->storage_->config(), ETIMEDOUT);
}

platform::linux::Status QualificationSession::preflight_zero_target_cia402() noexcept {
    struct Entry {
        std::uint16_t index;
        std::uint8_t subindex;
        std::uint32_t expected;
    };
    // This executor decodes status first, then packed speed. Never infer mapping from DLC alone.
    constexpr std::array contract{Entry{0x1800U, 1U, 0x181U},      Entry{0x1800U, 2U, 255U},
                                  Entry{0x1800U, 5U, 100U},        Entry{0x1A00U, 0U, 2U},
                                  Entry{0x1A00U, 1U, 0x60410020U}, Entry{0x1A00U, 2U, 0x606C0320U}};
    if (lifecycle_->storage_->config().tpdo_expected_dlc[0] != 8U) {
        return failure("qualification_tpdo_contract", lifecycle_->storage_->config(), EPROTO);
    }
    for (const auto entry : contract) {
        const auto value = upload(entry.index, entry.subindex, true);
        if (!value.ok()) {
            return value.status();
        }
        if (load_u32(std::as_bytes(std::span{value.value().data})) != entry.expected) {
            return failure("qualification_tpdo_contract", lifecycle_->storage_->config(), EPROTO);
        }
    }
    constexpr std::array<std::uint16_t, 5> indices{0x6060U, 0x6061U, 0x60FFU, 0x60FFU, 0x603FU};
    constexpr std::array<std::uint8_t, 5> parts{0U, 0U, 1U, 2U, 0U};
    for (std::size_t i = 0; i < indices.size(); ++i) {
        const auto value = upload(indices[i], parts[i], true);
        if (!value.ok()) {
            return value.status();
        }
        const std::array<std::uint8_t, 4> expected{i < 2U ? std::uint8_t{3U} : std::uint8_t{0U}, 0U, 0U, 0U};
        if (value.value().data != expected) {
            return failure("qualification_zero_initial_value", lifecycle_->storage_->config(), EPROTO);
        }
    }
    return require_zero_velocity_feedback(true);
}

platform::linux::Status
QualificationSession::enter_zero_target_operation_enabled(const std::chrono::milliseconds transition_timeout) noexcept {
    auto status = verify_target(IndependentChannel::subindex_1, 0, true);
    if (status.ok()) {
        status = verify_target(IndependentChannel::subindex_2, 0, true);
    }
    const auto operational_at = std::chrono::steady_clock::now();
    if (status.ok()) {
        status = send_nmt_raw(QualificationNmt::operational, true);
    }
    if (status.ok()) {
        status = wait_nmt_state(RemoteNmtState::operational, operational_at, transition_timeout);
    }
    if (status.ok()) {
        status = set_velocity_mode();
    }
    constexpr std::array commands{TransitionControlword::shutdown, TransitionControlword::switch_on,
                                  TransitionControlword::enable_operation};
    constexpr std::array states{domain::drive::Cia402State::ready_to_switch_on, domain::drive::Cia402State::switched_on,
                                domain::drive::Cia402State::operation_enabled};
    for (std::size_t i = 0; status.ok() && i < commands.size(); ++i) {
        if (i != 0U) {
            status = require_motion_feedback();
        }
        const auto command_at = std::chrono::steady_clock::now();
        if (status.ok()) {
            status = send_controlword_raw(commands[i], true);
        }
        if (status.ok()) {
            status = wait_dual_state(states[i], command_at, transition_timeout);
        }
        if (status.ok()) {
            status = require_zero_velocity_feedback(true);
        }
    }
    return status;
}

platform::linux::Status
QualificationSession::cleanup_zero_target_cia402(const std::chrono::milliseconds transition_timeout) noexcept {
    state_ = QualificationState::cleanup_required;
    auto status = set_zero_targets();
    const auto shutdown_at = std::chrono::steady_clock::now();
    if (status.ok() && !terminal_controlword_submitted_) {
        status = send_controlword_raw(TransitionControlword::shutdown, false);
    }
    if (status.ok() && !terminal_controlword_submitted_) {
        status = wait_dual_state(domain::drive::Cia402State::ready_to_switch_on, shutdown_at, transition_timeout, true);
    }
    if (status.ok()) {
        status = wait_zero_velocity(transition_timeout);
    }
    const auto preop_at = std::chrono::steady_clock::now();
    // Always attempt the final inhibit even when terminal state verification failed.
    const auto preop = send_nmt_raw(QualificationNmt::pre_operational, false);
    if (!preop.ok()) {
        return preop;
    }
    if (!status.ok()) {
        return status;
    }
    return wait_nmt_state(RemoteNmtState::pre_operational, preop_at, transition_timeout);
}

platform::linux::Status
QualificationSession::qualify_zero_target_cia402(const std::chrono::milliseconds transition_timeout) noexcept {
    auto status = state_ == QualificationState::ready
                      ? platform::linux::Status::success()
                      : failure("qualification_inhibited", lifecycle_->storage_->config(), EACCES);
    if (!status.ok()) {
        return finish_online_sequence(status);
    }
    if (transition_timeout <= 0ms || transition_timeout > 5s || target_sequence_consumed_) {
        return finish_online_sequence(
            failure("qualification_zero_bounds_or_consumed", lifecycle_->storage_->config(), EINVAL));
    }
    target_sequence_consumed_ = true;
    sequence_generation_ = lifecycle_->observation_snapshot(std::chrono::steady_clock::now()).generation;
    status = require_fresh_remote();
    if (!status.ok()) {
        status = start_online_heartbeat(transition_timeout);
    }
    if (!status.ok()) {
        return finish_online_sequence(status);
    }
    status = preflight_zero_target_cia402();
    if (!status.ok()) {
        return finish_online_sequence(status);
    }
    status = enter_zero_target_operation_enabled(transition_timeout);
    if (!status.ok()) {
        const auto restored = cleanup();
        status.context += restored.ok() ? " cleanup=submitted_unverified"
                                        : " cleanup_failed=" + restored.operation + ":" + restored.error.message();
        return finish_online_sequence(status);
    }
    return finish_online_sequence(cleanup_zero_target_cia402(transition_timeout));
}

platform::linux::Status QualificationSession::run_target_interval(const IndependentChannel channel,
                                                                  const std::int32_t rpm,
                                                                  const std::chrono::milliseconds duration) noexcept {
    auto status = require_operation_enabled_feedback(channel);
    if (!status.ok()) {
        return status;
    }
    const auto deadline = std::chrono::steady_clock::now() + duration;
    auto next_velocity_sample = std::chrono::steady_clock::now() + 100ms;
    status = verify_target(channel, rpm, true, deadline);
    if (!status.ok()) {
        static_cast<void>(verify_target(channel, 0, false));
        return status;
    }
    while (status.ok() && std::chrono::steady_clock::now() < deadline) {
        const auto step_deadline =
            std::min(deadline, std::chrono::steady_clock::now() + qualification_supervision_step);
        const auto run = lifecycle_->run_until(step_deadline);
        if (!run.ok() || run.value() != LifecycleExit::deadline) {
            status = run.ok() ? failure("qualification_owner_exit", lifecycle_->storage_->config(), ECANCELED)
                              : run.status();
            break;
        }
        status = require_operation_enabled_feedback(channel);
        const auto now = std::chrono::steady_clock::now();
        if (status.ok() && now >= next_velocity_sample && deadline - now > 100ms) {
            next_velocity_sample = now + 500ms;
            // Capture independent and packed raw feedback without extending the zero deadline.
            // The external CAN captures retain timestamps and values; do not log in this loop.
            for (const std::uint8_t part : {std::uint8_t{1U}, std::uint8_t{2U}, std::uint8_t{3U}}) {
                const auto velocity = upload(0x606CU, part, true,
                                             std::min(deadline, std::chrono::steady_clock::now() + 20ms));
                if (!velocity.ok()) {
                    status = velocity.status();
                    break;
                }
                status = require_operation_enabled_feedback(channel);
                if (!status.ok()) {
                    break;
                }
            }
        }
    }
    const auto zero = verify_target(channel, 0, false);
    if (!zero.ok()) {
        return zero;
    }
    return status;
}

platform::linux::Status QualificationSession::run_target_once(const IndependentChannel channel, const std::int32_t rpm,
                                                              const std::chrono::milliseconds duration) noexcept {
    auto ready = require_ready();
    if (!ready.ok()) {
        return inhibit(ready);
    }
    if (target_sequence_consumed_) {
        return inhibit(failure("qualification_target_consumed", lifecycle_->storage_->config(), EALREADY));
    }
    target_sequence_consumed_ = true;
    if (rpm == 0 || rpm < -qualification_target_limit_rpm || rpm > qualification_target_limit_rpm || duration <= 0ms
        || duration > qualification_duration_limit) {
        return inhibit(failure("qualification_target_bounds", lifecycle_->storage_->config(), ERANGE));
    }
    sequence_generation_ = lifecycle_->observation_snapshot(std::chrono::steady_clock::now()).generation;
    auto status = preflight_zero_target_cia402();
    if (!status.ok()) {
        return inhibit(status);
    }
    status = verify_target(IndependentChannel::subindex_1, 0, true);
    if (status.ok()) {
        status = verify_target(IndependentChannel::subindex_2, 0, true);
    }
    if (status.ok()) {
        status = run_target_interval(channel, rpm, duration);
    }
    return status.ok() ? status : inhibit(status);
}

platform::linux::Status
QualificationSession::cleanup_first_motion_cia402(const std::chrono::milliseconds transition_timeout) noexcept {
    rpdo_interval_ = false;
    auto status = cleanup_zero_target_cia402(transition_timeout);
    if (rpdo_mapping_owned_) {
        const auto restored = configure_rpdo_mapping(true);
        if (!restored.ok()) {
            if (status.ok()) { status = restored; }
            else { status.context += " mapping_restore_failed=" + restored.operation; }
        } else {
            rpdo_mapping_owned_ = false;
        }
    }
    const auto heartbeat_restored = restore_heartbeat_producer();
    if (status.ok() && !heartbeat_restored.ok()) {
        status = heartbeat_restored;
    }
    return status;
}

platform::linux::Status
QualificationSession::qualify_first_motion_cia402(const IndependentChannel channel, const std::int32_t rpm,
                                                  const std::chrono::milliseconds duration,
                                                  const std::chrono::milliseconds transition_timeout,
                                                  const bool use_rpdo) noexcept {
    auto status = state_ == QualificationState::ready
                      ? platform::linux::Status::success()
                      : failure("qualification_inhibited", lifecycle_->storage_->config(), EACCES);
    if (!status.ok()) {
        return finish_online_sequence(status);
    }
    if ((channel != IndependentChannel::subindex_1 && channel != IndependentChannel::subindex_2)
        || target_sequence_consumed_ || rpm == 0 || rpm < -qualification_target_limit_rpm
        || rpm > qualification_target_limit_rpm || duration <= 0ms || duration > qualification_duration_limit
        || transition_timeout <= 0ms || transition_timeout > 5s) {
        return finish_online_sequence(
            failure("qualification_first_motion_bounds_or_consumed", lifecycle_->storage_->config(), ERANGE));
    }
    target_sequence_consumed_ = true;
    sequence_generation_ = lifecycle_->observation_snapshot(std::chrono::steady_clock::now()).generation;
    status = require_fresh_remote();
    if (!status.ok()) {
        status = start_online_heartbeat(transition_timeout);
    }
    if (!status.ok()) {
        return finish_online_sequence(status);
    }
    status = preflight_zero_target_cia402();
    if (!status.ok()) {
        return finish_online_sequence(status);
    }
    const auto application = upload(0x200FU, 0U, true);
    if (!application.ok()) {
        return finish_online_sequence(application.status());
    }
    if (application.value().data != std::array<std::uint8_t, 4>{1U, 0U, 0U, 0U}) {
        return finish_online_sequence(
            failure("qualification_command_application_baseline", lifecycle_->storage_->config(), EPROTO));
    }

    synchronous_targets_ = true;
    if (use_rpdo) {
        status = preflight_rpdo_mapping();
        if (!status.ok()) {
            return finish_online_sequence(status);
        }
        const auto preop_at = std::chrono::steady_clock::now();
        status = send_nmt_raw(QualificationNmt::pre_operational, true);
        if (status.ok()) {
            status = wait_nmt_state(RemoteNmtState::pre_operational, preop_at, transition_timeout);
        }
        if (status.ok()) {
            rpdo_mapping_owned_ = true;
            status = configure_rpdo_mapping(false);
        }
        if (!status.ok()) {
            if (rpdo_mapping_owned_) {
                const auto restored = configure_rpdo_mapping(true);
                rpdo_mapping_owned_ = !restored.ok();
                status.context += restored.ok() ? " mapping_restore=verified"
                    : " mapping_restore_failed=" + restored.operation + ":" + restored.error.message();
            }
            return finish_online_sequence(status);
        }
    }
    if (status.ok()) {
        status = enter_zero_target_operation_enabled(transition_timeout);
    }
    if (status.ok()) {
        rpdo_interval_ = use_rpdo;
        status = run_target_interval(channel, rpm, duration);
    }
    const auto cleaned = cleanup_first_motion_cia402(transition_timeout);
    if (!status.ok()) {
        status.context +=
            cleaned.ok() ? " cleanup=verified" : " cleanup_failed=" + cleaned.operation + ":" + cleaned.error.message();
        return inhibit(status);
    }
    return inhibit(cleaned);
}

platform::linux::Status
QualificationSession::restore_zero_after_nmt_stop(const std::chrono::milliseconds transition_timeout,
                                                  const bool reenter_operational) noexcept {
    const auto preop_at = std::chrono::steady_clock::now();
    auto status = send_nmt_raw(QualificationNmt::pre_operational, false);
    if (!status.ok()) {
        return status;
    }
    status = set_zero_targets();
    if (!status.ok()) {
        return status;
    }
    status = wait_nmt_state(RemoteNmtState::pre_operational, preop_at, transition_timeout);
    if (!status.ok() || !reenter_operational) {
        return status;
    }
    const auto operational_at = std::chrono::steady_clock::now();
    const auto operational = send_nmt_raw(QualificationNmt::operational, false);
    if (!operational.ok()) {
        return operational;
    }
    return wait_nmt_state(RemoteNmtState::operational, operational_at, transition_timeout);
}

platform::linux::Status QualificationSession::run_motion_interval(const IndependentChannel channel,
                                                                  const std::int32_t rpm,
                                                                  const std::chrono::milliseconds duration) noexcept {
    auto status = require_operation_enabled_feedback(channel);
    if (!status.ok()) {
        return status;
    }
    const auto deadline = std::chrono::steady_clock::now() + duration;
    status = verify_target(channel, rpm, true, deadline);
    if (!status.ok()) {
        static_cast<void>(verify_target(channel, 0, false));
        return status;
    }
    while (status.ok() && std::chrono::steady_clock::now() < deadline) {
        const auto run = lifecycle_->run_until(
            std::min(deadline, std::chrono::steady_clock::now() + qualification_supervision_step));
        if (!run.ok() || run.value() != LifecycleExit::deadline) {
            status = run.ok() ? failure("qualification_owner_exit", lifecycle_->storage_->config(), ECANCELED)
                              : run.status();
            break;
        }
        status = require_operation_enabled_feedback(channel);
    }
    if (!status.ok()) {
        static_cast<void>(verify_target(channel, 0, false));
    }
    return status;
}

platform::linux::Status
QualificationSession::run_nmt_stop_interval(const IndependentChannel channel, const std::int32_t rpm,
                                            const std::chrono::milliseconds duration,
                                            const std::chrono::milliseconds transition_timeout) noexcept {
    auto status = run_motion_interval(channel, rpm, duration);
    if (!status.ok()) {
        return status;
    }

    const auto stopped_at = std::chrono::steady_clock::now();
    status = send_nmt_raw(QualificationNmt::stopped, true);
    if (!status.ok()) {
        static_cast<void>(verify_target(channel, 0, false));
        return status;
    }
    const auto stopped_timeout = std::min(transition_timeout, qualification_duration_limit - duration);
    status = wait_nmt_state(RemoteNmtState::stopped, stopped_at, stopped_timeout);
    const auto restored = restore_zero_after_nmt_stop(transition_timeout, status.ok());
    if (!status.ok()) {
        status.context += restored.ok() ? " zero_restore=verified"
                                        : " zero_restore_failed=" + restored.operation + ":" + restored.error.message();
        return status;
    }
    return restored;
}

platform::linux::Status
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters) -- private call site passes named duration values.
QualificationSession::run_controlword_stop_interval(const IndependentChannel channel, const std::int32_t rpm,
                                                    const std::chrono::milliseconds duration,
                                                    const std::chrono::milliseconds transition_timeout,
                                                    const TransitionControlword controlword,
                                                    const domain::drive::Cia402State expected_state) noexcept {
    auto status = run_motion_interval(channel, rpm, duration);
    if (!status.ok()) {
        return status;
    }
    const auto controlword_at = std::chrono::steady_clock::now();
    terminal_controlword_submitted_ = true;
    status = send_controlword_raw(controlword, true);
    if (!status.ok()) {
        return status;
    }
    status = wait_dual_state(expected_state, controlword_at, transition_timeout, true);
    if (!status.ok()) {
        return status;
    }
    const auto deadline = std::chrono::steady_clock::now() + transition_timeout;
    do {
        status = require_zero_velocity_feedback(false, deadline);
        if (status.ok() || status.operation != "qualification_nonzero_velocity") {
            break;
        }
        const auto run = lifecycle_->run_until(std::min(deadline, std::chrono::steady_clock::now() + 50ms));
        if (!run.ok() || run.value() != LifecycleExit::deadline) {
            return run.ok() ? failure("qualification_owner_exit", lifecycle_->storage_->config(), ECANCELED)
                            : run.status();
        }
    } while (std::chrono::steady_clock::now() < deadline);
    return !status.ok() && status.operation == "qualification_nonzero_velocity"
               ? failure("qualification_zero_velocity_timeout", lifecycle_->storage_->config(), ETIMEDOUT)
               : status;
}

platform::linux::Status
QualificationSession::qualify_nmt_stop_cia402(const IndependentChannel channel, const std::int32_t rpm,
                                              const std::chrono::milliseconds duration,
                                              const std::chrono::milliseconds transition_timeout) noexcept {
    return qualify_stop_cia402(channel, rpm, duration, transition_timeout, StopStimulus::nmt_stopped);
}

platform::linux::Status
QualificationSession::qualify_shutdown_cia402(const IndependentChannel channel, const std::int32_t rpm,
                                              const std::chrono::milliseconds duration,
                                              const std::chrono::milliseconds transition_timeout) noexcept {
    return qualify_stop_cia402(channel, rpm, duration, transition_timeout, StopStimulus::shutdown);
}

platform::linux::Status
QualificationSession::qualify_disable_voltage_cia402(const IndependentChannel channel, const std::int32_t rpm,
                                                     const std::chrono::milliseconds duration,
                                                     const std::chrono::milliseconds transition_timeout) noexcept {
    return qualify_stop_cia402(channel, rpm, duration, transition_timeout, StopStimulus::disable_voltage);
}

platform::linux::Status
QualificationSession::qualify_quick_stop_cia402(const IndependentChannel channel, const std::int32_t rpm,
                                                const std::chrono::milliseconds duration,
                                                const std::chrono::milliseconds transition_timeout) noexcept {
    return qualify_stop_cia402(channel, rpm, duration, transition_timeout, StopStimulus::quick_stop);
}

platform::linux::Status QualificationSession::set_communication_setting(const std::uint16_t index,
                                                                        const std::uint8_t object_subindex,
                                                                        const std::uint16_t value) noexcept {
    const std::array bytes{std::byte{static_cast<std::uint8_t>(value)},
                           std::byte{static_cast<std::uint8_t>(value >> 8U)}};
    return verify_download(index, object_subindex, bytes, index, object_subindex, bytes, false);
}

platform::linux::Status QualificationSession::wait_zero_velocity(const std::chrono::milliseconds timeout) noexcept {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    do {
        const auto status = require_zero_velocity_feedback(false, deadline);
        if (status.ok() || status.operation != "qualification_nonzero_velocity") {
            return status;
        }
        const auto run = lifecycle_->run_until(std::min(deadline, std::chrono::steady_clock::now() + 50ms));
        if (!run.ok() || run.value() != LifecycleExit::deadline) {
            return run.ok() ? failure("qualification_owner_exit", lifecycle_->storage_->config(), ECANCELED)
                            : run.status();
        }
    } while (std::chrono::steady_clock::now() < deadline);
    return failure("qualification_zero_velocity_timeout", lifecycle_->storage_->config(), ETIMEDOUT);
}

platform::linux::Status
QualificationSession::require_independent_motion_feedback(const IndependentChannel channel) noexcept {
    auto status = require_operation_enabled_feedback(channel);
    if (!status.ok()) {
        return status;
    }
    const auto deadline = std::chrono::steady_clock::now() + 100ms;
    for (const std::uint8_t part : std::array<std::uint8_t, 2>{1U, 2U}) {
        const auto value = upload(0x606CU, part, true, deadline);
        if (!value.ok()) {
            return value.status();
        }
        const bool zero = value.value().data == std::array<std::uint8_t, 4>{};
        if (part == static_cast<std::uint8_t>(channel) && zero) {
            return failure("qualification_motion_not_observed", lifecycle_->storage_->config(), ENODATA);
        }
        if (part != static_cast<std::uint8_t>(channel) && !zero) {
            return failure("qualification_other_channel_velocity", lifecycle_->storage_->config(), ERANGE);
        }
    }
    return require_operation_enabled_feedback(channel);
}

platform::linux::Status QualificationSession::observe_communication_loss(const StopStimulus stimulus) noexcept {
    const bool watchdog = stimulus == StopStimulus::watchdog;
    const auto deadline = std::chrono::steady_clock::now() + (watchdog ? 1500ms : 750ms);
    while (std::chrono::steady_clock::now() < deadline) {
        const auto run = lifecycle_->run_until(
            std::min(deadline, std::chrono::steady_clock::now() + qualification_supervision_step));
        if (!run.ok() || run.value() != LifecycleExit::deadline) {
            return run.ok() ? failure("qualification_owner_exit", lifecycle_->storage_->config(), ECANCELED)
                            : run.status();
        }
        const auto clean = require_clean_generation();
        if (!clean.ok()) {
            return clean;
        }
        const auto now = std::chrono::steady_clock::now();
        const auto snapshot = lifecycle_->observation_snapshot(now);
        const bool heartbeat_current = snapshot.heartbeat.frame.current;
        // Heartbeat expiry intentionally invalidates TPDO eligibility. Raw timestamps distinguish continued
        // wire arrival from eligibility without changing the production observation contract.
        const auto& tpdo = snapshot.tpdo[0];
        const bool tpdo_current = tpdo.present && tpdo.raw.generation == snapshot.generation
                                  && now - tpdo.raw.received_at <= lifecycle_->storage_->config().tpdo_timeout;
        if ((stimulus != StopStimulus::heartbeat_loss && !heartbeat_current)
            || (stimulus != StopStimulus::tpdo_loss && !tpdo_current)) {
            return failure("qualification_unexpected_feedback_loss", lifecycle_->storage_->config(), ENODATA);
        }
        if (!watchdog && tpdo_current) {
            const auto& bytes = tpdo.raw.payload;
            const auto state = domain::drive::decode_dual_axis_status(
                static_cast<std::uint32_t>(bytes[0]) | (static_cast<std::uint32_t>(bytes[1]) << 8U)
                | (static_cast<std::uint32_t>(bytes[2]) << 16U) | (static_cast<std::uint32_t>(bytes[3]) << 24U));
            if (state.low_half.state != domain::drive::Cia402State::operation_enabled
                || state.high_half.state != domain::drive::Cia402State::operation_enabled) {
                return failure("qualification_motion_not_enabled", lifecycle_->storage_->config(), EPROTO);
            }
        }
        if (tpdo_current && (tpdo.raw.payload[4] != 0U || tpdo.raw.payload[5] != 0U)) {
            return failure("qualification_other_channel_velocity", lifecycle_->storage_->config(), ERANGE);
        }
        if (!watchdog
            && ((stimulus == StopStimulus::heartbeat_loss && !heartbeat_current)
                || (stimulus == StopStimulus::tpdo_loss && !tpdo_current))) {
            return platform::linux::Status::success();
        }
    }
    if (!watchdog) {
        return failure("qualification_expected_feedback_loss_absent", lifecycle_->storage_->config(), ETIMEDOUT);
    }
    // Do not refresh the watchdog with an SDO probe while a nonzero target may still be retained.
    // The loop above required continuously fresh TPDOs; the first subsequent TX is cleanup's packed zero.
    const auto snapshot = lifecycle_->observation_snapshot(std::chrono::steady_clock::now());
    const auto& bytes = snapshot.tpdo[0].raw.payload;
    const bool zero = std::all_of(bytes.begin() + 4, bytes.end(), [](const auto value) { return value == 0U; });
    return zero ? platform::linux::Status::success()
                : failure("qualification_nonzero_velocity", lifecycle_->storage_->config(), ERANGE);
}

platform::linux::Status
QualificationSession::qualify_communication_loss_cia402(const CommunicationLossStimulus stimulus) noexcept {
    if (lifecycle_->storage_->config().heartbeat_timeout != 500ms
        || lifecycle_->storage_->config().tpdo_timeout != 100ms) {
        return inhibit(failure("qualification_communication_deadlines", lifecycle_->storage_->config(), EINVAL));
    }
    StopStimulus selected{};
    switch (stimulus) {
        case CommunicationLossStimulus::watchdog:
            selected = StopStimulus::watchdog;
            break;
        case CommunicationLossStimulus::heartbeat:
            selected = StopStimulus::heartbeat_loss;
            break;
        case CommunicationLossStimulus::tpdo:
            selected = StopStimulus::tpdo_loss;
            break;
        default:
            return inhibit(failure("qualification_communication_stimulus", lifecycle_->storage_->config(), EINVAL));
    }
    return qualify_stop_cia402(IndependentChannel::subindex_2, 5, 200ms, 2000ms, selected);
}

platform::linux::Status QualificationSession::qualify_stop_cia402(const IndependentChannel channel,
                                                                  const std::int32_t rpm,
                                                                  const std::chrono::milliseconds duration,
                                                                  const std::chrono::milliseconds transition_timeout,
                                                                  const StopStimulus stimulus) noexcept {
    if (state_ != QualificationState::ready) {
        return inhibit(failure("qualification_inhibited", lifecycle_->storage_->config(), EACCES));
    }
    if ((channel != IndependentChannel::subindex_1 && channel != IndependentChannel::subindex_2)
        || target_sequence_consumed_ || rpm == 0 || rpm < -qualification_target_limit_rpm
        || rpm > qualification_target_limit_rpm || duration <= 0ms
        || duration > (stimulus == StopStimulus::nmt_stopped ? 2s : qualification_shutdown_duration_limit)
        || transition_timeout <= 0ms || transition_timeout > 5s) {
        return inhibit(failure(stimulus == StopStimulus::nmt_stopped ? "qualification_nmt_stop_bounds_or_consumed"
                               : stimulus == StopStimulus::shutdown  ? "qualification_shutdown_bounds_or_consumed"
                               : stimulus == StopStimulus::disable_voltage
                                   ? "qualification_disable_voltage_bounds_or_consumed"
                                   : "qualification_quick_stop_bounds_or_consumed",
                               lifecycle_->storage_->config(), ERANGE));
    }
    target_sequence_consumed_ = true;
    sequence_generation_ = lifecycle_->observation_snapshot(std::chrono::steady_clock::now()).generation;
    auto status = require_clean_generation();
    if (!status.ok()) {
        return inhibit(status);
    }
    const bool communication_loss = stimulus == StopStimulus::watchdog || stimulus == StopStimulus::heartbeat_loss
                                    || stimulus == StopStimulus::tpdo_loss;
    bool watchdog_restore_required = false;
    bool feedback_restore_required = false;
    if (communication_loss) {
        const auto baseline = upload(0x2000U, 0U, false);
        if (!baseline.ok()) {
            return inhibit(baseline.status());
        }
        if (baseline.value().data != std::array<std::uint8_t, 4>{}) {
            return inhibit(failure("qualification_watchdog_baseline", lifecycle_->storage_->config(), EPROTO));
        }
        if (stimulus == StopStimulus::tpdo_loss) {
            const auto event_timer = upload(0x1800U, 5U, false);
            if (!event_timer.ok()) {
                return inhibit(event_timer.status());
            }
            if (event_timer.value().data != std::array<std::uint8_t, 4>{100U, 0U, 0U, 0U}) {
                return inhibit(failure("qualification_tpdo_timer_baseline", lifecycle_->storage_->config(), EPROTO));
            }
        }
    }
    if (stimulus == StopStimulus::quick_stop) {
        const auto option = upload(0x605AU, 0U, false);
        if (!option.ok()) {
            return inhibit(option.status());
        }
        if (option.value().data != std::array<std::uint8_t, 4>{5U, 0U, 0U, 0U}) {
            return inhibit(failure("qualification_quick_stop_option_baseline", lifecycle_->storage_->config(), EPROTO));
        }
    }
    const auto inhibit_after_heartbeat = [this](platform::linux::Status failed) {
        return finish_online_sequence(std::move(failed));
    };
    status = start_online_heartbeat(transition_timeout);
    if (!status.ok()) {
        return inhibit_after_heartbeat(status);
    }
    status = preflight_zero_target_cia402();
    if (!status.ok()) {
        return inhibit_after_heartbeat(status);
    }
    const auto application = upload(0x200FU, 0U, true);
    if (!application.ok()) {
        return inhibit_after_heartbeat(application.status());
    }
    if (application.value().data != std::array<std::uint8_t, 4>{1U, 0U, 0U, 0U}) {
        return inhibit_after_heartbeat(
            failure("qualification_command_application_baseline", lifecycle_->storage_->config(), EPROTO));
    }
    synchronous_targets_ = true;
    status = enter_zero_target_operation_enabled(transition_timeout);
    if (status.ok()) {
        switch (stimulus) {
            case StopStimulus::nmt_stopped:
                status = run_nmt_stop_interval(channel, rpm, duration, transition_timeout);
                break;
            case StopStimulus::shutdown:
                status = run_controlword_stop_interval(channel, rpm, duration, transition_timeout,
                                                       TransitionControlword::shutdown,
                                                       domain::drive::Cia402State::ready_to_switch_on);
                break;
            case StopStimulus::disable_voltage:
                status = run_controlword_stop_interval(channel, rpm, duration, transition_timeout,
                                                       TransitionControlword::disable_voltage,
                                                       domain::drive::Cia402State::switch_on_disabled);
                break;
            case StopStimulus::quick_stop:
                status = run_controlword_stop_interval(channel, rpm, duration, transition_timeout,
                                                       TransitionControlword::quick_stop,
                                                       domain::drive::Cia402State::quick_stop_active);
                break;
            case StopStimulus::watchdog:
            case StopStimulus::heartbeat_loss:
            case StopStimulus::tpdo_loss:
                watchdog_restore_required = true;
                status = set_communication_setting(0x2000U, 0U, 1000U);
                if (status.ok()) {
                    status = run_motion_interval(channel, rpm, duration);
                }
                if (status.ok()) {
                    status = require_independent_motion_feedback(channel);
                }
                if (status.ok() && stimulus != StopStimulus::watchdog) {
                    feedback_restore_required = true;
                    status = stimulus == StopStimulus::heartbeat_loss ? set_heartbeat_producer_raw(0U, false)
                                                                      : set_communication_setting(0x1800U, 5U, 0U);
                }
                if (status.ok()) {
                    status = observe_communication_loss(stimulus);
                }
                break;
        }
    }
    auto feedback_restored = platform::linux::Status::success();
    if (feedback_restore_required) {
        feedback_restored = set_zero_targets();
        if (feedback_restored.ok()) {
            feedback_restored = wait_zero_velocity(transition_timeout);
        }
        if (feedback_restored.ok()) {
            const auto restored_at = std::chrono::steady_clock::now();
            feedback_restored = stimulus == StopStimulus::heartbeat_loss ? set_heartbeat_producer_raw(500U, false)
                                                                         : set_communication_setting(0x1800U, 5U, 100U);
            if (feedback_restored.ok() && stimulus == StopStimulus::heartbeat_loss) {
                feedback_restored = wait_heartbeat(restored_at, transition_timeout);
            }
        }
    }
    auto cleaned = cleanup_first_motion_cia402(transition_timeout);
    if (!feedback_restored.ok()) {
        cleaned = feedback_restored;
    }
    if (watchdog_restore_required && cleaned.ok()) {
        cleaned = set_communication_setting(0x2000U, 0U, 0U);
    } else if (watchdog_restore_required) {
        cleaned.context += " watchdog_retained=1000_or_unverified operator_power_cut_required";
    }
    if (!status.ok()) {
        status.context += cleaned.ok() ? " cleanup=verified"
                                       : " cleanup_failed=" + cleaned.operation + ":" + cleaned.error.message() + " "
                                             + cleaned.context;
        return inhibit(status);
    }
    return inhibit(cleaned);
}

/** Configure the operator-supplied TPDO order without enabling or commanding either motor. */
platform::linux::Status QualificationSession::capture_manual_tpdo(const std::chrono::milliseconds duration) noexcept {
    using platform::linux::Status;
    if (state_ != QualificationState::ready || duration <= 0ms || duration > 60s
        || lifecycle_->storage_->config().tpdo_expected_dlc[0] != 8U) {
        return inhibit(failure("qualification_manual_bounds", lifecycle_->storage_->config(), EINVAL));
    }
    state_ = QualificationState::cleanup_required;
    sequence_generation_ = lifecycle_->observation_snapshot(std::chrono::steady_clock::now()).generation;
    auto deadline = std::chrono::steady_clock::now() + 8s;
    /** Read one exact value with the current phase deadline. */
    const auto read = [&](const std::uint16_t index, const std::uint8_t sub) {
        return upload(index, sub, false, deadline);
    };
    /** Compare the complete expedited value against the fixed baseline. */
    const auto expect = [&](const std::uint16_t index, const std::uint8_t sub, const std::uint32_t expected) {
        const auto value = read(index, sub);
        if (!value.ok()) {
            return value.status();
        }
        return load_u32(std::as_bytes(std::span{value.value().data})) == expected
                   ? Status::success()
                   : failure("qualification_manual_baseline", lifecycle_->storage_->config(), EPROTO);
    };
    /** Require both power stages to remain in reviewed non-enabled states. */
    const auto disabled = [&](const std::uint32_t raw) {
        for (const unsigned shift : {0U, 16U}) {
            const auto state = (raw >> shift) & 0x6FU;
            if (state != 0x21U && state != 0x40U && state != 0x60U) {
                return false;
            }
        }
        return true;
    };
    const auto initial_status = read(0x6041U, 0U);
    if (!initial_status.ok()) {
        return initial_status.status();
    }
    if (!disabled(load_u32(std::as_bytes(std::span{initial_status.value().data})))) {
        return failure("qualification_manual_drive_enabled", lifecycle_->storage_->config(), EACCES);
    }
    struct Entry {
        std::uint16_t index;
        std::uint8_t sub;
        std::uint32_t value;
    };
    constexpr std::array baseline{
        Entry{0x6060U, 0U, 3U},     Entry{0x6061U, 0U, 3U},          Entry{0x60FFU, 1U, 0U},
        Entry{0x60FFU, 2U, 0U},     Entry{0x603FU, 0U, 0U},          Entry{0x606CU, 1U, 0U},
        Entry{0x606CU, 2U, 0U},     Entry{0x606CU, 3U, 0U},          Entry{0x1017U, 0U, 0U},
        Entry{0x1800U, 1U, 0x181U}, Entry{0x1800U, 2U, 255U},        Entry{0x1800U, 5U, 100U},
        Entry{0x1A00U, 0U, 2U},     Entry{0x1A00U, 1U, 0x60410020U}, Entry{0x1A00U, 2U, 0x606C0320U}};
    for (const auto entry : baseline) {
        const auto status = expect(entry.index, entry.sub, entry.value);
        if (!status.ok()) {
            return status;
        }
    }
    auto status = require_clean_generation();
    if (!status.ok()) {
        return status;
    }
    /** Write and read back one gated exact value. */
    const auto write = [&](const Entry entry) {
        const auto size = robot_control_canopen_qualification_upload_size({entry.index, entry.sub});
        std::array<std::byte, 4> bytes{};
        for (unsigned i = 0U; i < 4U; ++i) {
            bytes[i] = std::byte{static_cast<std::uint8_t>(entry.value >> (8U * i))};
        }
        const auto data = std::span{bytes}.first(size);
        return verify_download(entry.index, entry.sub, data, entry.index, entry.sub, data, false, deadline);
    };
    /** Switch NMT and require a newer heartbeat without assuming any TPDO byte order. */
    const auto nmt = [&](const QualificationNmt command) {
        const auto sent_at = std::chrono::steady_clock::now();
        auto result = send_nmt_raw(command, false);
        if (!result.ok()) {
            return result;
        }
        const auto until = std::min(deadline, sent_at + 1s);
        while (std::chrono::steady_clock::now() < until) {
            const auto snapshot = lifecycle_->observation_snapshot(std::chrono::steady_clock::now());
            const auto expected = command == QualificationNmt::operational ? RemoteNmtState::operational
                                                                           : RemoteNmtState::pre_operational;
            if (snapshot.nmt.current && snapshot.nmt.state == expected && snapshot.nmt.raw.received_at > sent_at) {
                return Status::success();
            }
            const auto run = lifecycle_->run_until(std::min(until, std::chrono::steady_clock::now() + 1ms));
            if (!run.ok() || run.value() != LifecycleExit::deadline) {
                return run.ok() ? failure("qualification_owner_exit", lifecycle_->storage_->config(), ECANCELED)
                                : run.status();
            }
        }
        return failure("qualification_manual_nmt_timeout", lifecycle_->storage_->config(), ETIMEDOUT);
    };
    status = write({0x1017U, 0U, 500U});
    if (status.ok()) {
        status = nmt(QualificationNmt::pre_operational);
    }
    const auto operational_at = std::chrono::steady_clock::now();
    if (status.ok()) {
        status = nmt(QualificationNmt::operational);
    }
    // Establish an actual fresh stream before asking the operator to rotate the wheel.
    const auto ready_until = std::min(deadline, std::chrono::steady_clock::now() + 1s);
    while (status.ok()) {
        const auto snapshot = lifecycle_->observation_snapshot(std::chrono::steady_clock::now());
        if (snapshot.tpdo[0].current && snapshot.tpdo[0].raw.received_at > operational_at) {
            break;
        }
        if (std::chrono::steady_clock::now() >= ready_until) {
            status = failure("qualification_manual_tpdo_missing", lifecycle_->storage_->config(), ETIMEDOUT);
            break;
        }
        const auto run = lifecycle_->run_until(std::min(ready_until, std::chrono::steady_clock::now() + 1ms));
        if (!run.ok() || run.value() != LifecycleExit::deadline) {
            status = run.ok() ? failure("qualification_owner_exit", lifecycle_->storage_->config(), ECANCELED)
                              : run.status();
        }
    }
    if (status.ok()) {
        const auto snapshot = lifecycle_->observation_snapshot(std::chrono::steady_clock::now());
        const auto actual_status = read(0x6041U, 0U);
        if (!actual_status.ok()) {
            status = actual_status.status();
        } else if (!disabled(load_u32(std::as_bytes(std::span{actual_status.value().data})))
                   || !disabled(load_u32(std::as_bytes(std::span{snapshot.tpdo[0].raw.payload}.first(4U))))) {
            status = failure("qualification_manual_drive_enabled", lifecycle_->storage_->config(), EACCES);
        }
        if (status.ok()) {
            status = require_clean_generation();
        }
    }
    if (status.ok()) {
        std::cout << "MANUAL_ROTATION_READY duration_ms=" << duration.count()
                  << " no_motor_commands=1 mapping=status_first"
                     " schedule_seconds=0-10:stationary,10-25:left,25-35:stationary,35-50:right,50-60:stationary"
                  << std::endl;
        const auto capture_started = std::chrono::steady_clock::now();
        deadline = capture_started + duration;
        auto next_sample = capture_started;
        while (status.ok() && std::chrono::steady_clock::now() < deadline) {
            const auto run = lifecycle_->run_until(std::min(deadline, std::chrono::steady_clock::now() + 10ms));
            if (!run.ok() || run.value() != LifecycleExit::deadline) {
                status = run.ok() ? failure("qualification_owner_exit", lifecycle_->storage_->config(), ECANCELED)
                                  : run.status();
                break;
            }
            status = require_clean_generation();
            if (!status.ok()) {
                break;
            }
            const auto snapshot = lifecycle_->observation_snapshot(std::chrono::steady_clock::now());
            const auto& tpdo = snapshot.tpdo[0];
            if (!tpdo.current || !disabled(load_u32(std::as_bytes(std::span{tpdo.raw.payload}.first(4U))))) {
                status = failure("qualification_manual_feedback", lifecycle_->storage_->config(), EPROTO);
                break;
            }
            if (std::chrono::steady_clock::now() >= next_sample && deadline - std::chrono::steady_clock::now() > 50ms) {
                next_sample = std::chrono::steady_clock::now() + 1s;
                const auto elapsed = std::chrono::steady_clock::now() - capture_started;
                const auto phase = elapsed < 10s   ? "stationary"
                                   : elapsed < 25s ? "left"
                                   : elapsed < 35s ? "stationary"
                                   : elapsed < 50s ? "right"
                                                   : "stationary";
                std::cout << "manual_sample elapsed_ms="
                          << std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count()
                          << " phase=" << phase
                          << " tpdo_status_raw=" << load_u32(std::as_bytes(std::span{tpdo.raw.payload}.first(4U)))
                          << " tpdo_velocity_raw="
                          << load_u32(std::as_bytes(std::span{tpdo.raw.payload}.subspan(4U, 4U))) << std::endl;
                const auto actual_status = read(0x6041U, 0U);
                if (!actual_status.ok()) {
                    status = actual_status.status();
                    break;
                }
                if (!disabled(load_u32(std::as_bytes(std::span{actual_status.value().data})))) {
                    status = failure("qualification_manual_drive_enabled", lifecycle_->storage_->config(), EACCES);
                    break;
                }
                for (const std::uint8_t part : {std::uint8_t{1U}, std::uint8_t{2U}, std::uint8_t{3U}}) {
                    const auto velocity = read(0x606CU, part);
                    if (!velocity.ok()) {
                        status = velocity.status();
                        break;
                    }
                    const auto raw = load_u32(std::as_bytes(std::span{velocity.value().data}));
                    std::cout << "manual_velocity sub=" << static_cast<unsigned>(part) << " raw=" << raw << '\n';
                }
            }
        }
        std::cout << "MANUAL_ROTATION_END" << std::endl;
    }
    // Rollback is attempted even after an applied write loses its acknowledgement.
    deadline = std::chrono::steady_clock::now() + 5s;
    auto restored = nmt(QualificationNmt::pre_operational);
    if (restored.ok()) {
        restored = expect(0x60FFU, 1U, 0U);
    }
    if (restored.ok()) {
        restored = expect(0x60FFU, 2U, 0U);
    }
    for (const auto entry : {Entry{0x603FU, 0U, 0U}, Entry{0x606CU, 1U, 0U}, Entry{0x606CU, 2U, 0U}}) {
        if (restored.ok()) {
            restored = expect(entry.index, entry.sub, entry.value);
        }
    }
    if (restored.ok()) {
        const auto final_status = read(0x6041U, 0U);
        restored = !final_status.ok() ? final_status.status()
                   : disabled(load_u32(std::as_bytes(std::span{final_status.value().data})))
                       ? Status::success()
                       : failure("qualification_manual_drive_enabled", lifecycle_->storage_->config(), EACCES);
    }
    const auto heartbeat = write({0x1017U, 0U, 0U});
    if (restored.ok()) {
        restored = heartbeat;
    }
    if (!restored.ok()) {
        restored.context += " manual_cleanup_unverified original=" + status.operation;
        return restored;
    }
    return status;
}

platform::linux::Status QualificationSession::cleanup() noexcept {
    state_ = QualificationState::cleanup_required;
    auto status = verify_target(IndependentChannel::subindex_1, 0, false);
    if (status.ok()) {
        status = verify_target(IndependentChannel::subindex_2, 0, false);
    }
    if (status.ok()) {
        status = send_controlword_raw(TransitionControlword::shutdown, false);
    }
    if (status.ok()) {
        status = send_nmt_raw(QualificationNmt::pre_operational, false);
    }
    return status;
}

} // namespace robot_control::communication::canopen
