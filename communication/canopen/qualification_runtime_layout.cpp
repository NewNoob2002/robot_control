#include "communication/canopen/qualification.hpp"
#include "communication/canopen/qualification_gate.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <iostream>

namespace robot_control::communication::canopen {
namespace {
using namespace std::chrono_literals;
using Clock = std::chrono::steady_clock;
using platform::linux::Status;
/** Decode only the bounded little-endian bytes passed by the caller. */
std::uint32_t raw(std::span<const std::uint8_t> bytes) {
    std::uint32_t value = 0;
    for (std::size_t i = 0; i < bytes.size(); ++i)
        value |= static_cast<std::uint32_t>(bytes[i]) << (8U * i);
    return value;
}
/** Accept the previously qualified disabled/ready states for both axes. */
bool disabled(std::uint32_t status) {
    for (unsigned shift : {0U, 16U}) {
        const auto state = (status >> shift) & 0x6fU;
        if (state != 0 && state != 0x21 && state != 0x40 && state != 0x60)
            return false;
    }
    return true;
}
struct Entry {
    std::uint16_t index;
    std::uint8_t sub;
    std::uint32_t value;
};
} // namespace

Status QualificationSession::capture_runtime_diagnostics(std::chrono::milliseconds duration) noexcept {
    /** Add operation and fixed node/interface identity to every local failure. */
    const auto error = [&](const char* op, int code) {
        return Status::from_errno(op, "interface=" + lifecycle_->storage_->config().interface_name + " node=1", code);
    };
    if (state_ != QualificationState::ready || duration < 50ms || duration > 2s
        || lifecycle_->storage_->config().remote_node_id != 1
        || lifecycle_->storage_->config().tpdo_expected_dlc != std::array<std::uint8_t, 4>{8, 5, 0, 0})
        return inhibit(error("runtime_diagnostics_bounds", EINVAL));
    state_ = QualificationState::cleanup_required;
    sequence_generation_ = lifecycle_->observation_snapshot(Clock::now()).generation;
    auto deadline = Clock::now() + 8s;
    /** Read one value only while the initial transport/boot generation still exists. */
    const auto read = [&](std::uint16_t index, std::uint8_t sub) {
        if (lifecycle_->observation_snapshot(Clock::now()).generation != sequence_generation_)
            return platform::linux::Result<SdoObservation>::failure(error("runtime_diagnostics_generation", ESTALE));
        return upload(index, sub, false, deadline);
    };
    /** Check the complete expected value, retaining raw readbacks in the application log. */
    const auto expect = [&](Entry entry) {
        const auto result = read(entry.index, entry.sub);
        if (!result.ok())
            return result.status();
        const auto value = raw(result.value().data);
        std::cout << "layout_read index=" << entry.index << " sub=" << static_cast<unsigned>(entry.sub)
                  << " raw=" << value << '\n';
        return value == entry.value ? Status::success() : error("runtime_diagnostics_baseline", EPROTO);
    };
    /** Check disabled status independently of equal zero velocity readings. */
    const auto check_disabled = [&]() {
        const auto result = read(0x6041, 0);
        if (!result.ok())
            return result.status();
        return disabled(raw(result.value().data)) ? Status::success() : error("runtime_diagnostics_enabled", EACCES);
    };
    auto status = check_disabled();
    constexpr std::array baseline{Entry{0x6060, 0, 3},     Entry{0x6061, 0, 3},          Entry{0x603f, 0, 0},
                                  Entry{0x60ff, 1, 0},     Entry{0x60ff, 2, 0},          Entry{0x606c, 1, 0},
                                  Entry{0x606c, 2, 0},     Entry{0x606c, 3, 0},          Entry{0x1017, 0, 0},
                                  Entry{0x1800, 1, 0x181}, Entry{0x1800, 2, 255},        Entry{0x1800, 5, 100},
                                  Entry{0x1a00, 0, 2},     Entry{0x1a00, 1, 0x60410020}, Entry{0x1a00, 2, 0x606c0320},
                                  Entry{0x1801, 1, 0x281}, Entry{0x1801, 2, 255},        Entry{0x1801, 5, 0},
                                  Entry{0x1a01, 0, 0},     Entry{0x1a01, 1, 0},          Entry{0x1a01, 2, 0}};
    for (auto entry : baseline) {
        if (status.ok())
            status = expect(entry);
    }
    if (status.ok())
        status = require_clean_generation();
    if (!status.ok())
        return status; // Nothing has been written; never alter an unknown baseline.
    /** Write exactly the allowlisted object width, then verify it without a retry. */
    const auto write = [&](Entry entry) {
        if (lifecycle_->observation_snapshot(Clock::now()).generation != sequence_generation_)
            return error("runtime_diagnostics_generation", ESTALE);
        const auto size = robot_control_canopen_qualification_upload_size({entry.index, entry.sub});
        std::array<std::byte, 4> bytes{};
        for (unsigned i = 0; i < 4; ++i)
            bytes[i] = static_cast<std::byte>(entry.value >> (8U * i));
        const auto data = std::span{bytes}.first(size);
        return verify_download(entry.index, entry.sub, data, entry.index, entry.sub, data, false, deadline);
    };
    /** Send one node-1 NMT transition and wait for a newer matching heartbeat. */
    const auto nmt = [&](QualificationNmt command) {
        if (lifecycle_->observation_snapshot(Clock::now()).generation != sequence_generation_)
            return error("runtime_diagnostics_generation", ESTALE);
        const auto sent = Clock::now();
        auto result = send_nmt_raw(command, false);
        const auto until = std::min(deadline, sent + 1s);
        while (result.ok() && Clock::now() < until) {
            const auto snapshot = lifecycle_->observation_snapshot(Clock::now());
            if (snapshot.generation != sequence_generation_)
                return error("runtime_diagnostics_generation", ESTALE);
            const auto expected = command == QualificationNmt::operational ? RemoteNmtState::operational
                                                                           : RemoteNmtState::pre_operational;
            if (snapshot.nmt.current && snapshot.nmt.state == expected && snapshot.nmt.raw.received_at > sent)
                return Status::success();
            const auto run = lifecycle_->run_until(std::min(until, Clock::now() + 1ms));
            if (!run.ok() || run.value() != LifecycleExit::deadline)
                return run.ok() ? error("runtime_diagnostics_signal", ECANCELED) : run.status();
        }
        return result.ok() ? error("runtime_diagnostics_nmt_timeout", ETIMEDOUT) : result;
    };
    bool mapping_owned = false;
    status = write({0x1017, 0, 500});
    if (status.ok())
        status = nmt(QualificationNmt::pre_operational);
    constexpr std::array setup{Entry{0x1801, 1, 0x80000281}, Entry{0x1a01, 0, 0}, Entry{0x1a01, 1, 0x60610008},
                               Entry{0x1a01, 2, 0x603f0020}, Entry{0x1a01, 0, 2}, Entry{0x1801, 5, 100},
                               Entry{0x1801, 1, 0x281}};
    for (auto entry : setup) {
        if (status.ok()) {
            mapping_owned = true; // The write may apply even when its ACK is lost.
            status = write(entry);
        }
    }
    const auto operational_at = Clock::now();
    if (status.ok())
        status = nmt(QualificationNmt::operational);
    unsigned samples = 0;
    auto last = Clock::time_point{};
    const auto ready_until = Clock::now() + 1s;
    auto capture_until = Clock::time_point::max();
    while (status.ok()) {
        const auto now = Clock::now();
        if (samples && now >= capture_until)
            break;
        if (!samples && now >= ready_until) {
            status = error("runtime_diagnostics_missing", ETIMEDOUT);
            break;
        }
        const auto run = lifecycle_->run_until(std::min(deadline, now + 1ms));
        if (!run.ok() || run.value() != LifecycleExit::deadline) {
            status = run.ok() ? error("runtime_diagnostics_signal", ECANCELED) : run.status();
            break;
        }
        status = require_clean_generation();
        const auto s = lifecycle_->observation_snapshot(Clock::now());
        if (!status.ok())
            break;
        if (samples && (!s.tpdo[0].current || !s.tpdo[1].current || !s.heartbeat.frame.current)) {
            status = error("runtime_diagnostics_stale", ETIMEDOUT);
            break;
        }
        if (s.tpdo[0].current && s.tpdo[0].raw.received_at > operational_at) {
            const auto p = std::span{s.tpdo[0].raw.payload};
            if (!disabled(raw(p.first(4))) || raw(p.subspan(4, 4)) != 0) {
                status = error("runtime_diagnostics_motion", EPROTO);
                break;
            }
        }
        if (s.tpdo[1].current && s.tpdo[1].raw.received_at > std::max(last, operational_at)) {
            const auto p = std::span{s.tpdo[1].raw.payload};
            if (p[0] != 3 || raw(p.subspan(1, 4)) != 0 || !s.tpdo[0].current || !s.heartbeat.frame.current) {
                status = error("runtime_diagnostics_payload", EPROTO);
                break;
            }
            last = s.tpdo[1].raw.received_at;
            if (samples++ == 0)
                capture_until = Clock::now() + duration;
        }
    }
    if (status.ok() && samples < 3)
        status = error("runtime_diagnostics_samples", ENODATA);
    std::cout << "runtime_diagnostics samples=" << samples << " duration_ms=" << duration.count() << '\n';
    // No motor command is issued, even on failure. Restore only under the original generation.
    deadline = Clock::now() + 5s;
    auto cleanup = nmt(QualificationNmt::pre_operational);
    constexpr std::array restore{Entry{0x1801, 1, 0x80000281}, Entry{0x1a01, 0, 0}, Entry{0x1a01, 1, 0},
                                 Entry{0x1a01, 2, 0},          Entry{0x1801, 5, 0}, Entry{0x1801, 1, 0x281}};
    if (mapping_owned) {
        for (auto entry : restore)
            if (cleanup.ok())
                cleanup = write(entry);
    }
    if (cleanup.ok())
        cleanup = check_disabled();
    for (auto entry : baseline)
        if (cleanup.ok() && entry.index != 0x1017)
            cleanup = expect(entry);
    // Do not remove feedback if restoration failed; report the retained heartbeat.
    if (cleanup.ok())
        cleanup = write({0x1017, 0, 0});
    if (!cleanup.ok()) {
        status = status.ok() ? cleanup : status;
        status.context += " restore_failed=" + cleanup.operation + " operator_power_off_required";
    } else {
        status.context += " restore=verified";
    }
    return status;
}
} // namespace robot_control::communication::canopen
