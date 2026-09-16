#pragma once

#include "input/sbus/protocol/parser.hpp"
#include "platform/linux/uart/serial_port.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>

namespace robot_control::input::sbus {

struct ReaderConfig {
    platform::linux::uart::SerialConfig serial{platform::linux::uart::BaudRate::baud_100000, true, true, true};
    std::chrono::milliseconds maximum_service_gap{50};
};

enum class Discontinuity : std::uint8_t { none, service_gap, backlog, partial_timeout };

/** Owned receive observation, never a command or proof of transmitter freshness. */
struct ReadBatch {
    std::array<std::byte, 256> raw{}; // Kernel PARMRK encoding, before unescaping.
    std::size_t raw_size{0};
    std::array<protocol::ParseResult, 256> events{};
    std::size_t event_count{0};
    std::chrono::steady_clock::time_point captured_at{};
    std::uint64_t session{0};
    Discontinuity discontinuity{Discontinuity::none};
};

/** Single-owner receive-only adapter; no threads, drive objects, or auto-reconnect. */
class Reader final {
  public:
    /** Construct a closed reader with no valid session. */
    Reader() noexcept = default;
    Reader(const Reader&) = delete;
    Reader& operator=(const Reader&) = delete;

    /**
     * Replace the port, flush queued bytes, and start a new nonzero session.
     * @param path Explicit device identity, owned after open.
     * @param config Serial settings and positive service-gap limit (at most 1 s).
     * PARMRK is mandatory. Alternate serial formats are for explicit diagnostics.
     * @return Status; any failure leaves the reader closed. Sessions never wrap.
     * Thread safety: Single-owner. Reopen never preserves partial frames.
     */
    [[nodiscard]] platform::linux::Status open(std::string path, ReaderConfig config = {});

    /**
     * Read at most 256 kernel bytes and return all ordered parser events.
     * @param timeout Wait in [0, 20] ms, also bounded by maximum_service_gap.
     * @param cancellation_fd Borrowed pollable cancellation fd, or -1.
     * @return Owned batch (empty on timeout), or error after closing the reader.
     * A discontinuity batch contains no events and starts a new session after
     * flushing input. Raw bytes, when present, remain diagnostic evidence only.
     * Timestamps are userspace receive time, not per-byte or transmitter time.
     * Undetected driver drops and small queues cannot be aged by this interface.
     * Thread safety: Single-owner; caller must handle errors/discontinuities
     * before accepting subsequent observations. No health is inferred here.
     */
    [[nodiscard]] platform::linux::Result<ReadBatch> read(std::chrono::milliseconds timeout, int cancellation_fd = -1);

    /**
     * Query the live driver's settings without changing them.
     * @return Owned settings or failure. Thread safety: Single-owner.
     */
    [[nodiscard]] platform::linux::Result<platform::linux::uart::SerialConfig> configuration() const noexcept;

  private:
    /** Clear all stream state, flush unread bytes, and advance the session. */
    [[nodiscard]] platform::linux::Status reset_stream();
    /** Close the descriptor and clear partial state after any receive failure. */
    [[nodiscard]] platform::linux::Result<ReadBatch> fail(platform::linux::Status status);

    platform::linux::uart::SerialPort port_{};
    protocol::Parser parser_{};
    ReaderConfig config_{};
    std::chrono::steady_clock::time_point last_service_{};
    std::chrono::steady_clock::time_point last_byte_{};
    std::uint64_t session_{0};
    bool marker_pending_{false};
};
} // namespace robot_control::input::sbus
