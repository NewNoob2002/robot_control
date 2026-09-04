#include "communication/canopen/commissioning.hpp"

#include "communication/canopen/commissioning_gate.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <limits>
#include <string>

namespace robot_control::communication::canopen {
namespace {

using namespace std::chrono_literals;

/** Increment one generation while reserving zero as invalid. */
void increment_nonzero(std::uint64_t& value) noexcept {
    ++value;
    if (value == 0U) {
        ++value;
    }
}

/** Convert one commissioning failure into the project status contract. */
platform::linux::Status failure(const char* operation, const StackConfig& config, const int error_number) {
    return platform::linux::Status::from_errno(
        operation, "interface=" + config.interface_name + " remote=" + std::to_string(config.remote_node_id),
        error_number);
}

} // namespace

CommissioningSession::CommissioningSession(Lifecycle& lifecycle) noexcept : lifecycle_{&lifecycle} {}

platform::linux::Status CommissioningSession::require_fresh_remote() const noexcept {
    if (lifecycle_->storage_->config().remote_node_id != 1U) {
        return failure("commissioning_precondition", lifecycle_->storage_->config(), EACCES);
    }
    const auto snapshot = lifecycle_->observation_snapshot(std::chrono::steady_clock::now());
    if (!snapshot.boot_observed || snapshot.generation.boot == 0U || !snapshot.heartbeat.frame.current) {
        return failure("commissioning_precondition", lifecycle_->storage_->config(), ENOTCONN);
    }
    return platform::linux::Status::success();
}

platform::linux::Status CommissioningSession::send_nmt(const CommissioningNmt command) noexcept {
    auto ready = require_fresh_remote();
    if (!ready.ok()) {
        return ready;
    }
    if (!robot_control_canopen_authorize_nmt(static_cast<std::uint8_t>(command))) {
        return failure("authorize_nmt", lifecycle_->storage_->config(), EACCES);
    }
    const auto result = CO_NMT_sendCommand(lifecycle_->storage_->stack()->NMT, static_cast<CO_NMT_command_t>(command),
                                           lifecycle_->storage_->config().remote_node_id);
    robot_control_canopen_clear_authorization();
    return result == CO_ERROR_NO ? platform::linux::Status::success()
                                 : failure("CO_NMT_sendCommand", lifecycle_->storage_->config(), EIO);
}

platform::linux::Result<SdoObservation>
CommissioningSession::upload(const std::uint16_t index, const std::uint8_t subindex, const bool retry_once) noexcept {
    const robot_control_canopen_sdo_object_t object{.index = index, .subindex = subindex};
    const std::uint8_t expected_size = robot_control_canopen_sdo_upload_size(object);
    if (expected_size == 0U) {
        return platform::linux::Result<SdoObservation>::failure(
            failure("authorize_sdo_upload", lifecycle_->storage_->config(), EACCES));
    }
    increment_nonzero(request_generation_);
    const unsigned int attempts = retry_once ? 2U : 1U;
    for (unsigned int attempt = 0U; attempt < attempts; ++attempt) {
        auto ready = require_fresh_remote();
        if (!ready.ok()) {
            return platform::linux::Result<SdoObservation>::failure(ready);
        }
        increment_nonzero(attempt_generation_);
        lifecycle_->observations_.begin_sdo_upload(request_generation_, attempt_generation_, index, subindex,
                                                   expected_size);
        CO_SDOclient_t* const client = lifecycle_->storage_->stack()->SDOclient;
        if (CO_SDOclientUploadInitiate(client, index, subindex,
                                       static_cast<std::uint16_t>(lifecycle_->storage_->config().sdo_timeout.count()),
                                       false)
                != CO_SDO_RT_ok_communicationEnd
            || !robot_control_canopen_authorize_sdo_upload(object)) {
            lifecycle_->observations_.cancel_sdo_upload();
            CO_SDOclientClose(client);
            return platform::linux::Result<SdoObservation>::failure(
                failure("CO_SDOclientUploadInitiate", lifecycle_->storage_->config(), EPROTO));
        }

        CO_SDO_abortCode_t abort_code = CO_SDO_AB_NONE;
        auto previous = std::chrono::steady_clock::now();
        CO_SDO_return_t result = CO_SDOclientUpload(client, 0U, false, &abort_code, nullptr, nullptr, nullptr);
        while (result > CO_SDO_RT_ok_communicationEnd) {
            const auto step_deadline = std::chrono::steady_clock::now() + 1ms;
            const auto run = lifecycle_->run_until(step_deadline);
            if (!run.ok() || run.value() != LifecycleExit::deadline) {
                lifecycle_->observations_.cancel_sdo_upload();
                CO_SDOclientClose(client);
                robot_control_canopen_clear_authorization();
                return platform::linux::Result<SdoObservation>::failure(
                    run.ok() ? failure("commissioning_owner_exit", lifecycle_->storage_->config(), ECANCELED)
                             : run.status());
            }
            const auto now = std::chrono::steady_clock::now();
            const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(now - previous).count();
            previous = now;
            result = CO_SDOclientUpload(
                client,
                static_cast<std::uint32_t>(std::clamp<std::int64_t>(
                    elapsed, 0, static_cast<std::int64_t>(std::numeric_limits<std::uint32_t>::max()))),
                false, &abort_code, nullptr, nullptr, nullptr);
        }
        robot_control_canopen_clear_authorization();
        const auto snapshot = lifecycle_->observation_snapshot(std::chrono::steady_clock::now());
        CO_SDOclientClose(client);
        if (result == CO_SDO_RT_ok_communicationEnd || result == CO_SDO_RT_endedWithServerAbort) {
            if (!snapshot.sdo_result.frame.current || snapshot.sdo_result.request_generation != request_generation_
                || snapshot.sdo_result.attempt_generation != attempt_generation_) {
                lifecycle_->observations_.cancel_sdo_upload();
                return platform::linux::Result<SdoObservation>::failure(
                    failure("correlate_sdo_upload", lifecycle_->storage_->config(), EPROTO));
            }
            return platform::linux::Result<SdoObservation>::success(snapshot.sdo_result);
        }

        lifecycle_->observations_.cancel_sdo_upload();
        if (attempt + 1U == attempts || abort_code != CO_SDO_AB_TIMEOUT) {
            return platform::linux::Result<SdoObservation>::failure(
                failure("CO_SDOclientUpload", lifecycle_->storage_->config(),
                        abort_code == CO_SDO_AB_TIMEOUT ? ETIMEDOUT : EPROTO));
        }
        const auto quarantine =
            lifecycle_->run_until(std::chrono::steady_clock::now() + lifecycle_->storage_->config().sdo_timeout);
        if (!quarantine.ok() || quarantine.value() != LifecycleExit::deadline) {
            return platform::linux::Result<SdoObservation>::failure(
                quarantine.ok() ? failure("commissioning_quarantine", lifecycle_->storage_->config(), ECANCELED)
                                : quarantine.status());
        }
    }
    return platform::linux::Result<SdoObservation>::failure(
        failure("commissioning_upload", lifecycle_->storage_->config(), EPROTO));
}

} // namespace robot_control::communication::canopen
