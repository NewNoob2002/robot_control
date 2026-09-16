#include "communication/canopen/runtime.hpp"
#include <algorithm>
#include <bit>
#include <cerrno>
#include <limits>
#include <linux/can.h>
#include <new>
#include <string>
#include <sys/socket.h>

namespace robot_control::communication::canopen {
namespace {
using domain::drive::RuntimeReason;
/** Decode one U32 slice from the fixed eight-byte diagnostic storage. */
std::uint32_t u32(const RawCanopenFrame& f, std::size_t offset) noexcept {
    std::uint32_t value = 0;
    for (unsigned i = 0; i < 4; ++i) {
        value |= static_cast<std::uint32_t>(f.payload[offset + i]) << (8U * i);
    }
    return value;
}
/** Require current generation and exact service identifier/width. */
bool matches(const FrameObservation& frame, ObservationGeneration generation, std::uint32_t identifier,
             std::uint8_t width) noexcept {
    return frame.present && frame.current && frame.raw.generation == generation && frame.raw.identifier == identifier
           && frame.raw.dlc == width;
}
} // namespace
RuntimeSession::RuntimeSession(Lifecycle& owner, domain::drive::RuntimeConfig config, ObservationGeneration generation,
                               std::uint64_t session) noexcept
    : lifecycle_{&owner}, policy_{config}, generation_{generation}, session_{session} {}
RuntimeSession::CreateResult RuntimeSession::create(Lifecycle& owner, domain::drive::RuntimeConfig config,
                                                    const RuntimeLayoutProof& proof) noexcept {
    const auto& stack = owner.storage_->config();
    const auto now = std::chrono::steady_clock::now();
    const auto snapshot = owner.observation_snapshot(now);
    const auto error = [&](const char* operation, int code) {
        return CreateResult::failure(platform::linux::Status::from_errno(
            operation, "interface=" + stack.interface_name + " remote=" + std::to_string(stack.remote_node_id), code));
    };
    if (owner.runtime_ != nullptr) {
        return error("runtime_owner_busy", EBUSY);
    }
    if (!domain::drive::valid_runtime_config(config) || stack.tpdo_expected_dlc[0] != 8
        || stack.tpdo_expected_dlc[1] != 5 || config.heartbeat_timeout > stack.heartbeat_timeout
        || config.feedback_timeout > stack.tpdo_timeout) {
        return error("runtime_config", EINVAL);
    }
    if (proof.generation != snapshot.generation || proof.generation.transport == 0
        || proof.verified_at.time_since_epoch() < std::chrono::steady_clock::duration::zero() || proof.verified_at > now
        || now - proof.verified_at >= config.heartbeat_timeout || proof.rpdo_cob_id != 0x200U + stack.remote_node_id
        || proof.status_cob_id != 0x180U + stack.remote_node_id
        || proof.diagnostics_cob_id != 0x280U + stack.remote_node_id || proof.rpdo_type != 255
        || proof.status_type != 255 || proof.diagnostics_type != 255 || proof.rpdo_count != 2 || proof.status_count != 2
        || proof.diagnostics_count != 2 || proof.command_application != 1
        || proof.rpdo_map != std::array<std::uint32_t, 2>{0x60400010U, 0x60FF0320U}
        || proof.status_map != std::array<std::uint32_t, 2>{0x60410020U, 0x606C0320U}
        || proof.diagnostics_map != std::array<std::uint32_t, 2>{0x60610008U, 0x603F0020U}) {
        return error("runtime_layout_proof", EINVAL);
    }
    if (owner.runtime_generation_ == std::numeric_limits<std::uint64_t>::max()) {
        return error("runtime_session_exhausted", EOVERFLOW);
    }
    auto result = std::unique_ptr<RuntimeSession>{
        new (std::nothrow) RuntimeSession(owner, config, proof.generation, ++owner.runtime_generation_)};
    if (!result) {
        return error("runtime_allocate", ENOMEM);
    }
    result->malformed_ = snapshot.malformed_count;
    result->replayed_ = snapshot.replay_count;
    result->future_ = snapshot.future_timestamp_count;
    const auto status = result->refresh(now);
    if (!status.ok()) {
        return CreateResult::failure(status);
    }
    owner.runtime_ = result.get();
    return CreateResult::success(std::move(result));
}
RuntimeSession::~RuntimeSession() {
    policy_.stop();
    if (lifecycle_->runtime_ == this) {
        lifecycle_->runtime_ = nullptr;
    }
}
platform::linux::Status RuntimeSession::failure(const char* operation, int code) const noexcept {
    const auto& config = lifecycle_->storage_->config();
    return platform::linux::Status::from_errno(operation,
                                               "interface=" + config.interface_name
                                                   + " remote=" + std::to_string(config.remote_node_id)
                                                   + " session=" + std::to_string(session_),
                                               code);
}
domain::drive::RuntimeState RuntimeSession::state() const noexcept {
    return policy_.state();
}
std::uint64_t RuntimeSession::session() const noexcept {
    return session_;
}
platform::linux::Status RuntimeSession::send() noexcept {
    const auto snapshot = lifecycle_->observations_.snapshot(std::chrono::steady_clock::now());
    if (!binding_current_ || snapshot.generation != generation_) {
        binding_current_ = false;
        policy_.stop(RuntimeReason::generation_changed);
        return failure("runtime_binding_expired", ESTALE);
    }
    const auto* module = lifecycle_->storage_->stack()->CANmodule;
    if (module->CANinterfaceCount != 1 || module->CANinterfaces[0].fd < 0) {
        policy_.inhibit(RuntimeReason::transport_error);
        return failure("runtime_endpoint", ENOTCONN);
    }
    // Recheck the decision deadline immediately before the syscall boundary.
    policy_.observe(policy_.state().feedback, std::chrono::steady_clock::now());
    const auto output = policy_.state().output;
    can_frame frame{};
    frame.can_id = 0x200U + lifecycle_->storage_->config().remote_node_id;
    frame.can_dlc = 6;
    std::transform(output.payload.begin(), output.payload.end(), frame.data, [](std::byte value) {
        return std::to_integer<std::uint8_t>(value);
    });
    errno = 0;
    const auto written = ::send(module->CANinterfaces[0].fd, &frame, CAN_MTU, MSG_DONTWAIT);
    const auto error = errno;
    if (written != CAN_MTU) {
        policy_.stop(RuntimeReason::transport_error);
        stop_attempted_ = true;
        stop_status_ = failure("runtime_rpdo_send", error == 0 ? EIO : error);
        return stop_status_;
    }
    return platform::linux::Status::success();
}
platform::linux::Status RuntimeSession::refresh(std::chrono::steady_clock::time_point now) noexcept {
    const auto s = lifecycle_->observations_.snapshot(now);
    if (s.generation != generation_) {
        binding_current_ = false;
        policy_.stop(RuntimeReason::generation_changed);
        return platform::linux::Status::success(); // New preflight/session required before more writes.
    }
    const auto node = lifecycle_->storage_->config().remote_node_id;
    const bool event_error =
        s.malformed_count != malformed_ || s.replay_count != replayed_ || s.future_timestamp_count != future_;
    malformed_ = s.malformed_count;
    replayed_ = s.replay_count;
    future_ = s.future_timestamp_count;
    const auto& status = s.tpdo[0];
    const auto& diagnostics = s.tpdo[1];
    const bool was_armed = policy_.state().armed;
    const auto& emcy = s.emergency;
    const bool emergency_clear = !emcy.frame.present
                                 || (emcy.frame.current && emcy.frame.raw.generation == s.generation
                                     && emcy.error_code == 0 && emcy.error_register == 0);
    policy_.observe({.generation = {s.generation.transport, s.generation.boot},
                     .version = s.version,
                     .heartbeat_at = s.heartbeat.frame.raw.received_at,
                     .status_at = status.raw.received_at,
                     .diagnostics_at = diagnostics.raw.received_at,
                     .status_raw = u32(status.raw, 0),
                     .velocity_raw = u32(status.raw, 4),
                     .fault_raw = u32(diagnostics.raw, 1),
                     .mode_raw = std::bit_cast<std::int8_t>(diagnostics.raw.payload[0]),
                     .current = !event_error && emergency_clear
                                && matches(s.heartbeat.frame, s.generation, 0x700U + node, 1)
                                && matches(status, s.generation, 0x180U + node, 8)
                                && matches(diagnostics, s.generation, 0x280U + node, 5),
                     .operational = s.nmt.current && s.nmt.state == RemoteNmtState::operational},
                    now);
    if (was_armed && !policy_.state().armed && binding_current_) {
        return send();
    }
    return platform::linux::Status::success();
}
RuntimeSession::SendResult RuntimeSession::submit(const RuntimeSubmission& submission) noexcept {
    auto status = refresh(std::chrono::steady_clock::now());
    if (!status.ok()) {
        return SendResult::failure(status);
    }
    if (!binding_current_ || policy_.state().stopped) {
        return SendResult::failure(failure("runtime_inactive", ESTALE));
    }
    if (submission.session != session_) {
        policy_.inhibit(RuntimeReason::decision_invalid);
        status = send();
        return status.ok() ? SendResult::failure(failure("runtime_session_replay", ESTALE))
                           : SendResult::failure(status);
    }
    static_cast<void>(policy_.evaluate(submission.request, std::chrono::steady_clock::now()));
    status = send();
    return status.ok() ? SendResult::success(policy_.state().output) : SendResult::failure(status);
}
platform::linux::Status RuntimeSession::stop() noexcept {
    if (stop_attempted_) {
        return stop_status_;
    }
    policy_.stop();
    stop_attempted_ = true;
    stop_status_ = !binding_current_ ? failure("runtime_stop_binding_expired", ESTALE) : send();
    return stop_status_;
}
} // namespace robot_control::communication::canopen
