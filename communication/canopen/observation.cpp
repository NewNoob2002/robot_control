#include "communication/canopen/observation.hpp"

#include <linux/can.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <mutex>

namespace robot_control::communication::canopen {
namespace {

/** Return the 11-bit identifier without Linux frame flags. */
std::uint32_t standard_identifier(const RawCanopenFrame& frame) noexcept {
    return frame.identifier & CAN_SFF_MASK;
}

/** Return whether the payload byte is one reviewed CANopen NMT state. */
bool valid_nmt_state(const std::uint8_t value) noexcept {
    return value == static_cast<std::uint8_t>(RemoteNmtState::initializing)
           || value == static_cast<std::uint8_t>(RemoteNmtState::stopped)
           || value == static_cast<std::uint8_t>(RemoteNmtState::operational)
           || value == static_cast<std::uint8_t>(RemoteNmtState::pre_operational);
}

/** Decode one little-endian 16-bit protocol value. */
std::uint16_t read_u16(const std::array<std::uint8_t, 8>& payload, const std::size_t offset) noexcept {
    return static_cast<std::uint16_t>(payload[offset])
           | static_cast<std::uint16_t>(static_cast<std::uint16_t>(payload[offset + 1U]) << 8U);
}

/** Decode one little-endian 32-bit protocol value. */
std::uint32_t read_u32(const std::array<std::uint8_t, 8>& payload, const std::size_t offset) noexcept {
    return static_cast<std::uint32_t>(payload[offset]) | (static_cast<std::uint32_t>(payload[offset + 1U]) << 8U)
           | (static_cast<std::uint32_t>(payload[offset + 2U]) << 16U)
           | (static_cast<std::uint32_t>(payload[offset + 3U]) << 24U);
}

/** Return whether one current frame remains fresh at the supplied time. */
bool fresh(const FrameObservation& observation, const ObservationGeneration generation,
           const std::chrono::steady_clock::time_point now, const std::chrono::milliseconds timeout) noexcept {
    return observation.present && observation.current && observation.raw.generation == generation
           && observation.raw.received_at <= now && now - observation.raw.received_at <= timeout;
}

/** Return one TPDO slot for a standard identifier, if configured. */
std::size_t tpdo_index(const std::uint32_t identifier, const std::uint8_t node_id) noexcept {
    constexpr std::array<std::uint32_t, 4> bases{0x180U, 0x280U, 0x380U, 0x480U};
    const auto found = std::ranges::find_if(bases, [identifier, node_id](const std::uint32_t base) {
        return identifier == base + node_id;
    });
    return found == bases.end() ? bases.size() : static_cast<std::size_t>(std::distance(bases.begin(), found));
}

} // namespace

ObservationStore::ObservationStore(const StackConfig& config) noexcept
    : remote_node_id_{config.remote_node_id}, heartbeat_timeout_{config.heartbeat_timeout},
      tpdo_timeout_{config.tpdo_timeout}, tpdo_expected_dlc_{config.tpdo_expected_dlc} {}

void ObservationStore::invalidate_remote() noexcept {
    state_.boot_observed = false;
    state_.boot.current = false;
    state_.nmt.current = false;
    state_.heartbeat.frame.current = false;
    state_.emergency.frame.current = false;
    state_.sdo_result.frame.current = false;
    sdo_token_.active = false;
    for (auto& tpdo : state_.tpdo) {
        tpdo.current = false;
    }
}

void ObservationStore::invalidate_heartbeat_dependents() noexcept {
    state_.nmt.current = false;
    state_.heartbeat.frame.current = false;
    for (auto& tpdo : state_.tpdo) {
        tpdo.current = false;
    }
}

void ObservationStore::invalidate_addressed(const std::uint32_t identifier) noexcept {
    if (identifier == 0x700U + remote_node_id_) {
        invalidate_heartbeat_dependents();
        return;
    }
    if (identifier == 0x080U + remote_node_id_) {
        state_.emergency.frame.current = false;
        return;
    }
    if (identifier == 0x580U + remote_node_id_) {
        state_.sdo_result.frame.current = false;
        sdo_token_.active = false;
        return;
    }
    const auto index = tpdo_index(identifier, remote_node_id_);
    if (index < state_.tpdo.size()) {
        state_.tpdo[index].current = false;
    }
}

void ObservationStore::record_malformed(const RawCanopenFrame& frame) noexcept {
    state_.malformed = {.present = true, .current = false, .raw = frame};
    ++state_.malformed_count;
    invalidate_addressed(standard_identifier(frame));
}

void ObservationStore::begin_transport() noexcept {
    const std::scoped_lock lock{mutex_};
    ++state_.generation.transport;
    state_.generation.boot = 0U;
    invalidate_remote();
    ++state_.version;
}

void ObservationStore::begin_sdo_upload(const std::uint64_t request_generation, const std::uint64_t attempt_generation,
                                        const std::uint16_t index, const std::uint8_t subindex,
                                        const std::uint8_t expected_size) noexcept {
    const std::scoped_lock lock{mutex_};
    state_.sdo_result.frame.current = false;
    sdo_token_ = {.active = request_generation != 0U && attempt_generation != 0U && expected_size >= 1U
                            && expected_size <= 4U,
                  .generation = state_.generation,
                  .request = request_generation,
                  .attempt = attempt_generation,
                  .index = index,
                  .subindex = subindex,
                  .expected_size = expected_size};
    ++state_.version;
}

void ObservationStore::cancel_sdo_upload() noexcept {
    const std::scoped_lock lock{mutex_};
    if (sdo_token_.active) {
        sdo_token_.active = false;
        state_.sdo_result.frame.current = false;
        ++state_.version;
    }
}

ObservationGeneration ObservationStore::generation() const noexcept {
    const std::scoped_lock lock{mutex_};
    return state_.generation;
}

void ObservationStore::ingest(RawCanopenFrame frame, const std::chrono::steady_clock::time_point now) noexcept {
    const std::scoped_lock lock{mutex_};
    if (frame.generation != state_.generation) {
        ++state_.replay_count;
        ++state_.version;
        return;
    }

    if (frame.received_at > now) {
        ++state_.future_timestamp_count;
        record_malformed(frame);
        ++state_.version;
        return;
    }

    if ((frame.identifier & CAN_ERR_FLAG) != 0U) {
        state_.can_error = {.present = true, .current = false, .raw = frame};
        ++state_.generation.transport;
        state_.generation.boot = 0U;
        invalidate_remote();
        ++state_.version;
        return;
    }

    if ((frame.identifier & (CAN_EFF_FLAG | CAN_RTR_FLAG)) != 0U || frame.dlc > frame.payload.size()) {
        record_malformed(frame);
        ++state_.version;
        return;
    }

    const auto identifier = standard_identifier(frame);
    if (identifier == 0x580U + remote_node_id_) {
        const auto reject = [&]() {
            state_.sdo_rejected = {.present = true, .current = false, .raw = frame};
            ++state_.sdo_rejection_count;
            ++state_.version;
        };
        if (frame.dlc != 8U || !sdo_token_.active || sdo_token_.generation != state_.generation
            || frame.payload[1] != static_cast<std::uint8_t>(sdo_token_.index)
            || frame.payload[2] != static_cast<std::uint8_t>(sdo_token_.index >> 8U)
            || frame.payload[3] != sdo_token_.subindex) {
            reject();
            return;
        }
        const bool abort = frame.payload[0] == 0x80U;
        const std::uint8_t expected_command =
            static_cast<std::uint8_t>(0x43U | ((4U - sdo_token_.expected_size) << 2U));
        if (!abort && frame.payload[0] != expected_command) {
            reject();
            return;
        }
        SdoObservation result{
            .frame = {.present = true, .current = true, .raw = frame},
            .outcome = abort ? SdoOutcome::abort : SdoOutcome::expedited_upload,
            .request_generation = sdo_token_.request,
            .attempt_generation = sdo_token_.attempt,
            .index = sdo_token_.index,
            .subindex = sdo_token_.subindex,
            .data_length = abort ? std::uint8_t{0U} : sdo_token_.expected_size,
            .abort_code = abort ? read_u32(frame.payload, 4U) : 0U,
        };
        if (!abort) {
            std::copy_n(frame.payload.begin() + 4, result.data_length, result.data.begin());
        }
        state_.sdo_result = result;
        sdo_token_.active = false;
        ++state_.version;
        return;
    }
    if (identifier == 0x700U + remote_node_id_) {
        if (frame.dlc != 1U || !valid_nmt_state(frame.payload[0])) {
            record_malformed(frame);
            ++state_.version;
            return;
        }

        const auto nmt_state = static_cast<RemoteNmtState>(frame.payload[0]);
        if (nmt_state == RemoteNmtState::initializing) {
            ++state_.generation.boot;
            frame.generation = state_.generation;
            invalidate_remote();
            state_.boot_observed = true;
            state_.boot = {.present = true, .current = true, .raw = frame};
            state_.nmt = {.present = true, .current = true, .state = nmt_state, .raw = frame};
            ++state_.version;
            return;
        }

        if (!state_.boot_observed) {
            state_.heartbeat.frame = {.present = true, .current = false, .raw = frame};
            ++state_.version;
            return;
        }

        if (state_.heartbeat.frame.present && frame.received_at < state_.heartbeat.frame.raw.received_at) {
            ++state_.replay_count;
            ++state_.version;
            return;
        }
        if (state_.heartbeat.frame.current
            && frame.received_at - state_.heartbeat.frame.raw.received_at > heartbeat_timeout_) {
            for (auto& tpdo : state_.tpdo) {
                tpdo.current = false;
            }
        }
        state_.heartbeat.frame = {.present = true, .current = true, .raw = frame};
        state_.nmt = {.present = true, .current = true, .state = nmt_state, .raw = frame};
        ++state_.version;
        return;
    }

    const auto index = tpdo_index(identifier, remote_node_id_);
    if (index < state_.tpdo.size()) {
        if (frame.dlc != tpdo_expected_dlc_[index]) {
            record_malformed(frame);
            ++state_.version;
            return;
        }
        auto& tpdo = state_.tpdo[index];
        if (tpdo.present && frame.received_at < tpdo.raw.received_at) {
            ++state_.replay_count;
            ++state_.version;
            return;
        }
        tpdo = {.present = true, .current = state_.boot_observed, .raw = frame};
        ++state_.version;
        return;
    }

    if (identifier == 0x080U + remote_node_id_) {
        if (frame.dlc != 8U) {
            record_malformed(frame);
            ++state_.version;
            return;
        }
        state_.emergency = {
            .frame = {.present = true, .current = state_.boot_observed, .raw = frame},
            .error_code = read_u16(frame.payload, 0U),
            .error_register = frame.payload[2],
            .error_bit = frame.payload[3],
            .info_code = read_u32(frame.payload, 4U),
        };
        ++state_.version;
    }
}

void ObservationStore::advance_time(const std::chrono::steady_clock::time_point now) noexcept {
    const std::scoped_lock lock{mutex_};
    bool changed = false;

    const FrameObservation nmt_frame{
        .present = state_.nmt.present,
        .current = state_.nmt.current,
        .raw = state_.nmt.raw,
    };
    if (state_.nmt.current && !fresh(nmt_frame, state_.generation, now, heartbeat_timeout_)) {
        state_.nmt.current = false;
        changed = true;
    }
    if (state_.heartbeat.frame.current && !fresh(state_.heartbeat.frame, state_.generation, now, heartbeat_timeout_)) {
        state_.heartbeat.frame.current = false;
        changed = true;
    }

    for (auto& tpdo : state_.tpdo) {
        if (tpdo.current && (!state_.heartbeat.frame.current || !fresh(tpdo, state_.generation, now, tpdo_timeout_))) {
            tpdo.current = false;
            changed = true;
        }
    }
    if (changed) {
        ++state_.version;
    }
}

ObservationSnapshot ObservationStore::snapshot(const std::chrono::steady_clock::time_point now) const noexcept {
    const std::scoped_lock lock{mutex_};
    auto snapshot = state_;
    snapshot.boot.current = snapshot.boot.current && snapshot.boot.raw.generation == snapshot.generation
                            && snapshot.boot.raw.received_at <= now;

    const bool heartbeat_fresh = fresh(snapshot.heartbeat.frame, snapshot.generation, now, heartbeat_timeout_);
    snapshot.heartbeat.frame.current = heartbeat_fresh;

    const FrameObservation nmt_frame{
        .present = snapshot.nmt.present, .current = snapshot.nmt.current, .raw = snapshot.nmt.raw};
    snapshot.nmt.current = fresh(nmt_frame, snapshot.generation, now, heartbeat_timeout_);
    if (!heartbeat_fresh) {
        for (auto& tpdo : snapshot.tpdo) {
            tpdo.current = false;
        }
    } else {
        for (auto& tpdo : snapshot.tpdo) {
            tpdo.current = fresh(tpdo, snapshot.generation, now, tpdo_timeout_);
        }
    }

    snapshot.emergency.frame.current = snapshot.emergency.frame.current
                                       && snapshot.emergency.frame.raw.generation == snapshot.generation
                                       && snapshot.emergency.frame.raw.received_at <= now;
    snapshot.sdo_result.frame.current = snapshot.sdo_result.frame.current
                                        && snapshot.sdo_result.frame.raw.generation == snapshot.generation
                                        && snapshot.sdo_result.frame.raw.received_at <= now;
    return snapshot;
}

} // namespace robot_control::communication::canopen
