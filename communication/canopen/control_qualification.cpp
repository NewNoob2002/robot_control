#include "communication/canopen/control_qualification.hpp"
#include "communication/canopen/qualification_gate.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cerrno>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <type_traits>

namespace robot_control::communication::canopen {
namespace {
using namespace std::chrono_literals;
using Clock = std::chrono::steady_clock;
using platform::linux::Status;
struct Entry {
    std::uint16_t index;
    std::uint8_t sub;
    std::uint32_t value;
};
/** Format an unsigned dictionary field without changing shared stream formatting; pure and reentrant. */
std::string hex_field(std::uint32_t value, int width) {
    std::ostringstream text;
    text << "0x" << std::hex << std::uppercase << std::setfill('0') << std::setw(width) << value;
    return text.str();
}
/** Compare signed independent32 or both packed16 feedback fields without changing raw evidence. */
bool within_standstill(std::uint32_t raw, bool packed, int tolerance) noexcept {
    const auto within = [tolerance](std::int32_t value) {
        return value >= -tolerance && value <= tolerance;
    };
    if (!packed)
        return within(std::bit_cast<std::int32_t>(raw));
    return within(std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(raw)))
           && within(std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(raw >> 16U)));
}
} // namespace
ControlQualification::ControlQualification(Lifecycle& owner) noexcept : owner_{owner}, operations_{owner} {}
Status ControlQualification::error(const char* operation, int code) const {
    return Status::from_errno(operation, "interface=" + owner_.storage_->config().interface_name + " node=1", code);
}
Status ControlQualification::current() const {
    if (owner_.runtime_ != nullptr)
        return error("control_hil_runtime_attached", EBUSY);
    if (owner_.observation_snapshot(Clock::now()).generation != generation_)
        return error("control_hil_generation_changed", ESTALE);
    return Clock::now() < deadline_ ? Status::success() : error("control_hil_phase_deadline", ETIMEDOUT);
}
platform::linux::Result<std::uint32_t> ControlQualification::read(std::uint16_t index, std::uint8_t sub) {
    using Result = platform::linux::Result<std::uint32_t>;
    const auto status = current();
    if (!status.ok())
        return Result::failure(status);
    const auto result = operations_.upload(index, sub, false, deadline_);
    if (!result.ok())
        return Result::failure(result.status());
    std::uint32_t value = 0;
    for (unsigned i = 0; i < 4; ++i)
        value |= static_cast<std::uint32_t>(result.value().data[i]) << (8U * i);
    std::cout << "hil_read index=" << hex_field(index, 4) << " sub=" << hex_field(sub, 2)
              << " raw=" << hex_field(value, 8) << '\n';
    return Result::success(value);
}
// Object index/subindex/value order matches the CANopen dictionary and existing SDO API.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
Status ControlQualification::expect(std::uint16_t index, std::uint8_t sub, std::uint32_t value) {
    const auto result = read(index, sub);
    if (!result.ok())
        return result.status();
    if (result.value() == value)
        return Status::success();
    auto failed = error("control_hil_baseline", EPROTO);
    failed.context +=
        " index=" + hex_field(index, 4) + " sub=" + hex_field(sub, 2) + " raw=" + hex_field(result.value(), 8);
    return failed;
}
// Object index/subindex/value order is validated again by the wire allowlist.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
Status ControlQualification::write(std::uint16_t index, std::uint8_t sub, std::uint32_t value) {
    auto status = current();
    if (!status.ok())
        return status;
    const auto width = robot_control_canopen_qualification_upload_size({index, sub});
    std::array<std::byte, 4> bytes{};
    for (unsigned i = 0; i < 4; ++i)
        bytes[i] = static_cast<std::byte>(value >> (8U * i));
    if (width == 0 || width > bytes.size())
        return error("control_hil_object_width", EINVAL);
    auto data = std::span{bytes}.first(width);
    return operations_.verify_download(index, sub, data, index, sub, data, false, deadline_);
}
Status ControlQualification::nmt(QualificationNmt command) {
    auto status = current();
    if (!status.ok())
        return status;
    const auto sent = Clock::now();
    status = operations_.send_nmt_raw(command, false);
    if (!status.ok())
        return status;
    const auto until = std::min(deadline_, sent + 1s);
    while (Clock::now() < until) {
        status = current();
        if (!status.ok())
            return status;
        const auto snapshot = owner_.observation_snapshot(Clock::now());
        const auto expected =
            command == QualificationNmt::operational ? RemoteNmtState::operational : RemoteNmtState::pre_operational;
        if (snapshot.nmt.current && snapshot.nmt.state == expected && snapshot.nmt.raw.received_at > sent)
            return Status::success();
        const auto run = owner_.run_until(std::min(until, Clock::now() + 1ms));
        if (!run.ok() || run.value() != LifecycleExit::deadline)
            return run.ok() ? error("control_hil_signal", ECANCELED) : run.status();
    }
    return error("control_hil_nmt_timeout", ETIMEDOUT);
}
Status ControlQualification::mapping(bool diagnostics, bool restore) {
    auto status = current();
    if (!status.ok())
        return status;
    const auto snapshot = owner_.observation_snapshot(Clock::now());
    if (!snapshot.nmt.current || snapshot.nmt.state != RemoteNmtState::pre_operational)
        return error("control_hil_mapping_nmt", EACCES);
    if (!diagnostics)
        return operations_.configure_rpdo_mapping(restore, deadline_);
    const std::array entries{Entry{0x1801, 1, 0x80000281},
                             Entry{0x1a01, 0, 0},
                             Entry{0x1a01, 1, restore ? 0U : 0x60610008U},
                             Entry{0x1a01, 2, restore ? 0U : 0x603f0020U},
                             Entry{0x1a01, 0, restore ? 0U : 2U},
                             Entry{0x1801, 5, restore ? 0U : 100U},
                             Entry{0x1801, 1, 0x281}};
    for (auto entry : entries) {
        status = write(entry.index, entry.sub, entry.value);
        if (!status.ok())
            return status;
    }
    return status;
}
Status ControlQualification::begin(int zero_tolerance_tenths_rpm) {
    if (started_ || owner_.storage_->config().remote_node_id != 1 || owner_.runtime_ != nullptr
        || owner_.storage_->config().tpdo_expected_dlc != std::array<std::uint8_t, 4>{8, 5, 0, 0})
        return error("control_hil_prepare_once", EACCES);
    if (zero_tolerance_tenths_rpm < 0 || zero_tolerance_tenths_rpm > 20)
        return error("control_hil_zero_tolerance", EINVAL);
    zero_tolerance_ = zero_tolerance_tenths_rpm;
    started_ = true;
    generation_ = owner_.observation_snapshot(Clock::now()).generation;
    operations_.sequence_generation_ = generation_;
    deadline_ = Clock::now() + 10s;
    const auto initial = read(0x6041, 0);
    if (!initial.ok())
        return initial.status();
    const auto decoded = domain::drive::decode_dual_axis_status(initial.value());
    if ((initial.value() & 0x8000U) != 0)
        return error("control_hil_x1_active", EACCES);
    for (auto state : {decoded.low_half.state, decoded.high_half.state}) {
        if (state != domain::drive::Cia402State::not_ready_to_switch_on
            && state != domain::drive::Cia402State::switch_on_disabled
            && state != domain::drive::Cia402State::ready_to_switch_on)
            return error("control_hil_initial_state", EACCES);
    }
    return Status::success();
}
Status ControlQualification::check_baseline(bool interrupted) {
    constexpr std::array baseline{Entry{0x6060, 0, 3},          Entry{0x6061, 0, 3},          Entry{0x603f, 0, 0},
                                  Entry{0x60ff, 1, 0},          Entry{0x60ff, 2, 0},          Entry{0x606c, 1, 0},
                                  Entry{0x606c, 2, 0},          Entry{0x606c, 3, 0},          Entry{0x200f, 0, 1},
                                  Entry{0x2000, 0, 0},          Entry{0x1017, 0, 0},          Entry{0x1400, 1, 0x201},
                                  Entry{0x1400, 2, 255},        Entry{0x1400, 5, 1000},       Entry{0x1600, 0, 2},
                                  Entry{0x1600, 1, 0x60400010}, Entry{0x1600, 2, 0x60600008}, Entry{0x1800, 1, 0x181},
                                  Entry{0x1800, 2, 255},        Entry{0x1800, 5, 100},        Entry{0x1a00, 0, 2},
                                  Entry{0x1a00, 1, 0x60410020}, Entry{0x1a00, 2, 0x606c0320}, Entry{0x1801, 1, 0x281},
                                  Entry{0x1801, 2, 255},        Entry{0x1801, 5, 0},          Entry{0x1a01, 0, 0},
                                  Entry{0x1a01, 1, 0},          Entry{0x1a01, 2, 0}};
    for (auto entry : baseline) {
        if (interrupted) {
            if (entry.index == 0x2000)
                entry.value = 1000;
            if (entry.index == 0x1017)
                entry.value = 500;
            if (entry.index == 0x1600 && entry.sub == 2)
                entry.value = 0x60ff0320;
            if (entry.index == 0x1801 && entry.sub == 5)
                entry.value = 100;
            if (entry.index == 0x1a01)
                entry.value = entry.sub == 0 ? 2U : entry.sub == 1 ? 0x60610008U : 0x603f0020U;
        }
        if (zero_tolerance_ != 0 && entry.index == 0x606c)
            continue;
        auto status = expect(entry.index, entry.sub, entry.value);
        if (!status.ok())
            return status;
    }
    if (zero_tolerance_ != 0) {
        const auto stationary = stable_standstill();
        if (!stationary.ok())
            return stationary;
    }
    return Status::success();
}
platform::linux::Result<RuntimeLayoutProof> ControlQualification::prepare(bool require_quick_stop_option,
                                                                          int zero_tolerance_tenths_rpm,
                                                                          int speed_resolution,
                                                                          const DeviceConfiguration* device) {
    using Result = platform::linux::Result<RuntimeLayoutProof>;
    if (speed_resolution < 0 || speed_resolution > 10)
        return Result::failure(error("speed_resolution_range", EINVAL));
    auto initial = begin(zero_tolerance_tenths_rpm);
    if (initial.ok())
        initial = check_baseline(false);
    if (!initial.ok())
        return Result::failure(initial);
    if (require_quick_stop_option) {
        const auto option = expect(0x605a, 0, 5);
        if (!option.ok())
            return Result::failure(option);
    }
    if (speed_resolution != 0) {
        const auto original = read(0x2026, 5);
        if (!original.ok())
            return Result::failure(original.status());
        if (original.value() < 1 || original.value() > 10)
            return Result::failure(error("speed_resolution_baseline", ERANGE));
        if (original.value() != static_cast<std::uint32_t>(speed_resolution))
            return Result::failure(error("device_configuration_required_speed_resolution_mismatch", EPROTO));
    }
#ifdef ROBOT_CONTROL_TEST_APP
    if (device != nullptr) {
        const auto checked = check_device_parameters(*device);
        if (!checked.ok())
            return Result::failure(checked);
    }
#else
    static_cast<void>(device);
#endif
    auto status = operations_.require_clean_generation();
    if (!status.ok())
        return Result::failure(status);
    heartbeat_owned_ = true;
    const auto heartbeat_started = Clock::now();
    status = write(0x1017, 0, 500);
    if (status.ok())
        status = operations_.wait_heartbeat(heartbeat_started, 1000ms);
    if (status.ok())
        status = nmt(QualificationNmt::pre_operational);
    if (status.ok()) {
        watchdog_owned_ = true;
        status = write(0x2000, 0, 1000);
    }
    if (status.ok()) {
        rpdo_owned_ = true;
        status = mapping(false, false);
    }
    if (status.ok()) {
        diagnostics_owned_ = true;
        status = mapping(true, false);
    }
    if (status.ok())
        status = nmt(QualificationNmt::operational);
    if (status.ok())
        status = write(0x60ff, 1, 0);
    if (status.ok())
        status = write(0x60ff, 2, 0);
    const auto shutdown_at = Clock::now();
    if (status.ok()) {
        motor_owned_ = true;
        status = write(0x6040, 0, 6);
    }
    if (status.ok())
        status = operations_.wait_dual_state(domain::drive::Cia402State::ready_to_switch_on, shutdown_at, 2000ms);
    if (!status.ok())
        return Result::failure(status);
    const auto feedback_deadline = std::min(deadline_, Clock::now() + 1s);
    while (status.ok()) {
        const auto snapshot = owner_.observation_snapshot(Clock::now());
        const auto& diagnostic = snapshot.tpdo[1];
        if (diagnostic.current && diagnostic.raw.received_at > shutdown_at) {
            if (diagnostic.raw.payload[0] != 3
                || std::any_of(diagnostic.raw.payload.begin() + 1, diagnostic.raw.payload.begin() + 5, [](auto byte) {
                       return byte != 0;
                   }))
                status = error("control_hil_diagnostics", EPROTO);
            break;
        }
        if (Clock::now() >= feedback_deadline) {
            status = error("control_hil_diagnostics_timeout", ETIMEDOUT);
            break;
        }
        const auto run = owner_.run_until(std::min(feedback_deadline, Clock::now() + 1ms));
        status = !run.ok()                                ? run.status()
                 : run.value() != LifecycleExit::deadline ? error("control_hil_signal", ECANCELED)
                                                          : current();
    }
    if (!status.ok())
        return Result::failure(status);
    RuntimeLayoutProof proof{.generation = generation_};
    /** Populate proof fields from actual correlated reads, never fixture constants. */
    const auto field = [&](std::uint16_t index, std::uint8_t sub, auto& target) {
        const auto value = read(index, sub);
        if (!value.ok())
            return value.status();
        target = static_cast<std::remove_reference_t<decltype(target)>>(value.value());
        return Status::success();
    };
    const auto fill = [&]() -> Status {
        for (unsigned slot = 0; slot < 3; ++slot) {
            const std::uint16_t comm = slot == 0 ? 0x1400 : slot == 1 ? 0x1800 : 0x1801;
            const std::uint16_t map = slot == 0 ? 0x1600 : slot == 1 ? 0x1a00 : 0x1a01;
            auto& cob = slot == 0 ? proof.rpdo_cob_id : slot == 1 ? proof.status_cob_id : proof.diagnostics_cob_id;
            auto& type = slot == 0 ? proof.rpdo_type : slot == 1 ? proof.status_type : proof.diagnostics_type;
            auto& count = slot == 0 ? proof.rpdo_count : slot == 1 ? proof.status_count : proof.diagnostics_count;
            auto& entries = slot == 0 ? proof.rpdo_map : slot == 1 ? proof.status_map : proof.diagnostics_map;
            auto result = field(comm, 1, cob);
            if (result.ok())
                result = field(comm, 2, type);
            if (result.ok())
                result = field(map, 0, count);
            if (result.ok())
                result = field(map, 1, entries[0]);
            if (result.ok())
                result = field(map, 2, entries[1]);
            if (!result.ok())
                return result;
        }
        return field(0x200f, 0, proof.command_application);
    };
    status = fill();
    if (status.ok() && speed_resolution != 0) {
        status = field(0x2026, 5, proof.speed_resolution);
        if (status.ok() && proof.speed_resolution != speed_resolution)
            status = error("speed_resolution_changed", EPROTO);
    }
    if (status.ok())
        status = current();
    if (!status.ok())
        return Result::failure(status);
    proof.verified_at = Clock::now();
    return Result::success(proof);
}
#ifdef ROBOT_CONTROL_TEST_APP
Status ControlQualification::check_device_parameters(const DeviceConfiguration& config) {
    if (!valid_device_configuration(config))
        return error("device_configuration_invalid", EINVAL);
    for (std::size_t i = 0; i < config.values.size(); ++i) {
        if (config.values[i] == -1)
            continue;
        const auto& p = device_parameters[i];
        auto status = expect(p.index, p.sub, static_cast<std::uint32_t>(config.values[i]));
        if (!status.ok()) {
            status.context += " device_configuration_required key=" + std::string{p.key};
            return status;
        }
    }
    return Status::success();
}
Status ControlQualification::device_pdo_baseline(bool configure) {
    if (configure) {
        const auto snapshot = owner_.observation_snapshot(Clock::now());
        if (!snapshot.nmt.current || snapshot.nmt.state != RemoteNmtState::pre_operational)
            return error("device_pdo_not_preoperational", EACCES);
    }
    for (unsigned slot = 0; slot < 3; ++slot) {
        const std::uint16_t comm = slot == 0 ? 0x1400 : slot == 1 ? 0x1800 : 0x1801;
        const std::uint16_t map = slot == 0 ? 0x1600 : slot == 1 ? 0x1a00 : 0x1a01;
        const std::uint32_t cob = slot == 0 ? 0x201 : slot == 1 ? 0x181 : 0x281;
        if (configure) {
            auto status = write(comm, 1, cob | 0x80000000U);
            if (status.ok())
                status = write(map, 0, 0);
            if (!status.ok())
                return status;
        }
        const std::array entries{Entry{map, 1,
                                       slot == 0   ? 0x60400010U
                                       : slot == 1 ? 0x60410020U
                                                   : 0U},
                                 Entry{map, 2,
                                       slot == 0   ? 0x60600008U
                                       : slot == 1 ? 0x606c0320U
                                                   : 0U},
                                 Entry{comm, 2, 255},
                                 Entry{comm, 5,
                                       slot == 0   ? 1000U
                                       : slot == 1 ? 100U
                                                   : 0U},
                                 Entry{map, 0, slot == 2 ? 0U : 2U},
                                 Entry{comm, 1, cob}};
        for (auto entry : entries) {
            const auto status =
                configure ? write(entry.index, entry.sub, entry.value) : expect(entry.index, entry.sub, entry.value);
            if (!status.ok())
                return status;
        }
    }
    return Status::success();
}
Status ControlQualification::configure_device(const DeviceConfiguration& config) {
    if (!valid_device_configuration(config))
        return error("device_configuration_invalid", EINVAL);
    auto status = begin(20);
    deadline_ = Clock::now() + 30s;
    // No enable/target/stop commands here: the operator must present a disabled, stationary device.
    constexpr std::array safe{Entry{0x6040, 0, 0}, Entry{0x603f, 0, 0}, Entry{0x60ff, 1, 0},
                              Entry{0x60ff, 2, 0}, Entry{0x1017, 0, 0}, Entry{0x2000, 0, 0}};
    for (auto entry : safe) {
        if (status.ok())
            status = expect(entry.index, entry.sub, entry.value);
    }
    if (status.ok())
        status = stable_standstill();
    if (status.ok() && !config.configure_pdo)
        status = device_pdo_baseline(false);
    std::array<std::uint32_t, device_parameters.size()> original{};
    for (std::size_t i = 0; status.ok() && i < config.values.size(); ++i) {
        if (config.values[i] == -1)
            continue;
        const auto& p = device_parameters[i];
        const auto value = read(p.index, p.sub);
        status = value.status();
        if (value.ok()) {
            original[i] = value.value();
            std::cout << "event=device_parameter key=" << p.key << " index=" << hex_field(p.index, 4)
                      << " sub=" << hex_field(p.sub, 2) << " before=" << hex_field(original[i], 8)
                      << " requested=" << hex_field(static_cast<std::uint32_t>(config.values[i]), 8) << '\n'
                      << std::flush;
        }
    }
    if (!status.ok())
        return status; // Complete all parameter reads before the first write.
    operations_.device_configuration_ = true;
    bool heartbeat_touched = false;
    if (config.configure_pdo) {
        heartbeat_touched = true;
        const auto started = Clock::now();
        status = write(0x1017, 0, 500);
        if (status.ok())
            status = operations_.wait_heartbeat(started, 1000ms);
        if (status.ok())
            status = nmt(QualificationNmt::pre_operational);
    }
    for (std::size_t i = 0; status.ok() && i < config.values.size(); ++i) {
        if (config.values[i] != -1 && original[i] != static_cast<std::uint32_t>(config.values[i]))
            status = write(device_parameters[i].index, device_parameters[i].sub,
                           static_cast<std::uint32_t>(config.values[i]));
    }
    if (status.ok() && config.configure_pdo)
        status = device_pdo_baseline(true);
    if (heartbeat_touched) {
        // Restore this temporary handshake before saving; retain both errors if restoration fails.
        const auto restored = write(0x1017, 0, 0);
        if (!restored.ok()) {
            if (status.ok())
                status = restored;
            else
                status.context += " heartbeat_restore_failed=" + restored.operation;
        }
    }
    if (status.ok())
        status = check_device_parameters(config);
    if (status.ok())
        status = device_pdo_baseline(false);
    if (status.ok())
        status = expect(0x2000, 0, 0);
    if (status.ok())
        status = current();
    if (status.ok()) {
        // Save is a self-clearing command: do not read-back or retry it.
        const std::array save{std::byte{1}, std::byte{0}};
        std::cout << "event=eeprom_save_requested index=0x2010 sub=0x00 value=0x0001\n" << std::flush;
        status = operations_.download(0x2010, 0, save, false, deadline_);
    }
    operations_.device_configuration_ = false;
    if (!status.ok())
        status.context += " configuration_or_save_unverified no_automatic_retry";
    return status;
}
#endif
Status ControlQualification::restore_interrupted(int zero_tolerance_tenths_rpm) {
    auto status = begin(zero_tolerance_tenths_rpm);
    if (status.ok())
        status = expect(0x6040, 0, 0);
    if (status.ok()) {
        const auto state = read(0x6041, 0);
        if (!state.ok())
            return state.status();
        const auto axes = domain::drive::decode_dual_axis_status(state.value());
        if (axes.low_half.state != domain::drive::Cia402State::switch_on_disabled
            || axes.high_half.state != domain::drive::Cia402State::switch_on_disabled)
            return error("control_hil_restore_state", EACCES);
    }
    if (status.ok())
        status = check_baseline(true);
    if (status.ok())
        status = expect(0x605a, 0, 5);
    if (status.ok())
        status = operations_.require_clean_generation();
    if (!status.ok())
        return status; // No ownership or write before the complete known residual matches.
    heartbeat_owned_ = watchdog_owned_ = rpdo_owned_ = diagnostics_owned_ = motor_owned_ = true;
    status = finish();
    if (status.ok())
        status = check_baseline(false);
    finish_status_ = status;
    return status;
}
Status ControlQualification::stable_standstill() {
    auto started = Clock::time_point{};
    // Every correlated SDO sample must be in range; an outlier does not restart the hold silently.
    do {
        for (std::uint8_t part : std::array<std::uint8_t, 3>{1, 2, 3}) {
            const auto value = read(0x606c, part);
            if (!value.ok())
                return value.status();
            if (!within_standstill(value.value(), part == 3, zero_tolerance_))
                return error("control_hil_standstill_feedback", ERANGE);
        }
        if (started == Clock::time_point{})
            started = Clock::now();
        if (Clock::now() - started >= 150ms) {
            std::cout << "event=standstill_verified tolerance_tenths_rpm=" << zero_tolerance_ << " elapsed_us="
                      << std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - started).count() << '\n';
            return Status::success();
        }
        const auto run = owner_.run_until(std::min(deadline_, Clock::now() + 20ms));
        if (!run.ok() || run.value() != LifecycleExit::deadline)
            return run.ok() ? error("control_hil_signal", ECANCELED) : run.status();
    } while (Clock::now() < deadline_);
    return error("control_hil_standstill_deadline", ETIMEDOUT);
}
Status ControlQualification::finish() {
    if (finished_)
        return finish_status_;
    // A caller sequencing mistake performs no I/O and may be corrected by detaching.
    if (owner_.runtime_ != nullptr)
        return error("control_hil_runtime_attached", EBUSY);
    finished_ = true;
    if (!heartbeat_owned_)
        return finish_status_;
    deadline_ = Clock::now() + 10s;
    auto status = current();
    if (status.ok() && motor_owned_) {
        status = write(0x60ff, 1, 0);
        if (status.ok())
            status = write(0x60ff, 2, 0);
        if (status.ok())
            status = write(0x6040, 0, 0);
        // SDO confirmation also works after partial setup in Pre-operational.
        const auto until = std::min(deadline_, Clock::now() + 2s);
        while (status.ok()) {
            const auto state = read(0x6041, 0);
            if (!state.ok()) {
                status = state.status();
                break;
            }
            const auto decoded = domain::drive::decode_dual_axis_status(state.value());
            if (decoded.low_half.state == domain::drive::Cia402State::switch_on_disabled
                && decoded.high_half.state == domain::drive::Cia402State::switch_on_disabled)
                break;
            if (Clock::now() >= until) {
                status = error("control_hil_disable_timeout", ETIMEDOUT);
                break;
            }
            const auto run = owner_.run_until(std::min(until, Clock::now() + 1ms));
            if (!run.ok() || run.value() != LifecycleExit::deadline) {
                status = run.ok() ? error("control_hil_signal", ECANCELED) : run.status();
                break;
            }
        }
    }
    if (status.ok())
        status =
            zero_tolerance_ == 0 ? operations_.require_zero_velocity_feedback(false, deadline_) : stable_standstill();
    if (status.ok()) {
        const auto feedback = owner_.observation_snapshot(Clock::now()).tpdo[0];
        std::uint32_t raw = 0;
        for (unsigned i = 0; i < 4; ++i)
            raw |= static_cast<std::uint32_t>(feedback.raw.payload[4 + i]) << (8U * i);
        if (feedback.current && !within_standstill(raw, true, zero_tolerance_))
            status = error("control_hil_cleanup_moving", ERANGE);
    }
    if (status.ok() && (rpdo_owned_ || diagnostics_owned_))
        status = nmt(QualificationNmt::pre_operational);
    if (status.ok() && rpdo_owned_)
        status = mapping(false, true);
    if (status.ok() && diagnostics_owned_)
        status = mapping(true, true);
    if (status.ok() && watchdog_owned_)
        status = write(0x2000, 0, 0);
    if (status.ok())
        status = write(0x1017, 0, 0);
    if (!status.ok())
        status.context += " control_hil_cleanup_unverified operator_power_off_required";
    finish_status_ = status;
    return status;
}
} // namespace robot_control::communication::canopen
