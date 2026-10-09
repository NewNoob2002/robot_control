#pragma once

#include "application/control/control_cycle.hpp"
#include "communication/canopen/device_parameters.hpp"
#include "input/sbus/source/source.hpp"

#include <iosfwd>
#include <span>
#include <string>
#include <string_view>

namespace robot_control::application::config {
/** Startup-owned configuration; no device, file descriptor or thread is retained. */
struct Startup {
    std::string interface;
    std::string device;
    std::string log_dir;
    std::string ipc_socket; // Empty disables the read-only IPC probe endpoint.
    std::chrono::milliseconds run_duration{};
    std::size_t log_file_bytes{67108864};
    unsigned log_files{4};
    communication::canopen::DeviceConfiguration drive{};
    control::CycleConfig cycle{};
    input::sbus::SourceConfig sbus{};
};

/**
 * Parse and validate the versioned test profile before any device activation.
 * @param input Borrowed key=value configuration stream; at most 16KiB.
 * @param overrides Borrowed key=value CLI overrides, each key at most once.
 * @return Owned configuration using existing Source and Cycle validators.
 * @throws std::invalid_argument On malformed, duplicate, unknown or unsafe fields.
 * @throws std::runtime_error On input stream failure.
 * Thread safety: Reentrant with separate streams; retains no input/override storage.
 */
[[nodiscard]] Startup parse(std::istream& input, std::span<const std::string_view> overrides = {});

/**
 * Write the effective profile and fixed bindings for startup diagnostics.
 * @param output Borrowed output stream; caller checks stream errors.
 * @param config Validated owned configuration, borrowed for this call only.
 * Thread safety: Call before the control loop; shared streams need external synchronization.
 */
void dump(std::ostream& output, const Startup& config);
} // namespace robot_control::application::config
