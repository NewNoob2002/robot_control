#pragma once

#include <spdlog/common.h>
#include <spdlog/sinks/base_sink.h>

#include <cstdint>
#include <mutex>

namespace robot_control::service::logging {

/** Linux stderr sink with nonblocking I/O and atomic error diagnostics. */
class StderrSink final : public spdlog::sinks::base_sink<std::mutex> {
  public:
    /** Construct a serialized stderr sink; output/health remain process-wide. */
    StderrSink() = default;
    /** Release formatter storage without closing the borrowed stderr descriptor. */
    ~StderrSink() override = default;

    StderrSink(const StderrSink&) = delete;
    StderrSink& operator=(const StderrSink&) = delete;
    StderrSink(StderrSink&&) = delete;
    StderrSink& operator=(StderrSink&&) = delete;

    /** Return the atomic process-wide failed-write count; safe during concurrent output. */
    [[nodiscard]] std::uint64_t failure_count() const noexcept;
    /** Return the last raw output errno; safe during concurrent output. */
    [[nodiscard]] int last_error() const noexcept;
    /** Reset process-wide diagnostics only during quiescent test setup. */
    void reset_health() noexcept;

  protected:
    /** Emit one formatted record without waiting for writable readiness; called with the sink lock. */
    void sink_it_(const spdlog::details::log_msg& msg) override;
    /** No userspace output buffer is retained; called with the sink lock. */
    void flush_() override;
};

} // namespace robot_control::service::logging
