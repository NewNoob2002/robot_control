#pragma once
#include <string>
#include <sys/types.h>
#include "app/record.hpp"
#include "application/config/startup.hpp"
#include "platform/linux/unique_fd.hpp"

namespace robot_control::app {
/** Own a separate disk-writing child and bounded nonblocking owner pipes; construct before device activation. */
class LogSession final {
  public:
    /** Create a private per-run directory, fork the collector, and redirect app output to a nonblocking pipe.
     * @param config Validated startup value; copied into the forked child, never borrowed after construction.
     * @throws std::exception Before device activation on directory/pipe/child readiness failure.
     * Thread safety: Construct on the composition thread before CAN or any worker exists.
     */
    explicit LogSession(const application::config::Startup& config);
    /** Bounded collector cleanup; never touches a device. Call finish explicitly to inspect its result. */
    ~LogSession();
    LogSession(const LogSession&) = delete;
    LogSession& operator=(const LogSession&) = delete;
    /** Borrow the single-owner recorder until this session is finished. */
    [[nodiscard]] RecordWriter& records() noexcept {
        return writer_;
    }
    /** Read collector heartbeat without waiting; disk stall, exit, full pipe or stream failure rejects health. */
    [[nodiscard]] bool healthy() noexcept;
    /** Close output, restore terminal descriptors and wait at most2s for collection; return verified EOF/footer success. */
    [[nodiscard]] bool finish(int result) noexcept;
    /** Return the owned run directory for user instructions and diagnostics. */
    [[nodiscard]] const std::string& directory() const noexcept {
        return directory_;
    }

  private:
    using Fd = platform::linux::UniqueFd;
    Fd raw_, heartbeat_, saved_out_, saved_err_;
    RecordWriter writer_{};
    std::string directory_;
    pid_t child_{-1};
    std::chrono::steady_clock::time_point last_heartbeat_{};
    bool finished_{false}, result_{false}, redirected_{false};
};
} // namespace robot_control::app
