#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace robot_control::input::sbus::protocol {

/** Raw fixed-profile SBUS observation; no health or motion authorization. */
struct Frame {
    std::array<std::uint16_t, 16> channels{};
    std::uint8_t raw_flags{0};
    bool digital_channel_17{false};
    bool digital_channel_18{false};
    bool frame_lost{false};
    bool failsafe{false};
};

enum class EventKind : std::uint8_t { none, frame, rejected };

/** One event and its consumed prefix; frame is meaningful only for EventKind::frame. */
struct ParseResult {
    std::size_t consumed{0};
    EventKind kind{EventKind::none};
    Frame frame{};
};

/** Diagnostic counts wrap modulo 2^64; they must never authorize input. */
struct Statistics {
    std::uint64_t bytes_consumed{0};
    std::uint64_t frames{0};
    std::uint64_t rejected_frames{0};
    std::uint64_t discarded_bytes{0};
    std::uint64_t lost_frames{0};
    std::uint64_t failsafe_frames{0};
    std::size_t buffered_bytes{0};
};

/** Single-owner, allocation-free 25-byte parser for header 0x0f and footer 0x00. */
class Parser final {
  public:
    /** Construct empty framing state and zero diagnostic counters. */
    Parser() noexcept = default;

    /**
     * Consume bytes up to and including the first complete or rejected candidate.
     *
     * @param bytes Borrowed chunk, retained only for this call; empty is allowed.
     * @return Owned event and consumed prefix length. Nonempty input always makes
     * progress; none consumes the whole chunk without publishing a frame.
     *
     * Call again with bytes.subspan(result.consumed) until the chunk is exhausted,
     * delivering every event in order (including lost/failsafe before recovery).
     * No timestamp is inferred: the source owns reception time and health policy.
     * Header/footer framing cannot detect all payload corruption or byte loss.
     * Thread safety: Single-owner only; input may be released on return.
     */
    [[nodiscard]] ParseResult consume(std::span<const std::uint8_t> bytes) noexcept;

    /**
     * Discard partial framing state and reset all diagnostic counters.
     *
     * Invoke on source restart or known stream discontinuity. This does not
     * restore source health or authorization. Thread safety: Single-owner only.
     */
    void reset() noexcept;

    /**
     * Copy diagnostic counts and current buffered length (always below 25).
     *
     * @return Owned snapshot, independent of subsequent parser changes.
     * Thread safety: Single-owner only; not concurrent with consume/reset.
     */
    [[nodiscard]] Statistics statistics() const noexcept;

  private:
    /** Decode the complete internal candidate; requires a valid header/footer. */
    [[nodiscard]] Frame decode() const noexcept;
    /** Retain the earliest subsequent header suffix after rejecting a candidate. */
    void resynchronize() noexcept;

    std::array<std::uint8_t, 25> buffer_{};
    std::size_t length_{0};
    Statistics statistics_{};
};

} // namespace robot_control::input::sbus::protocol
