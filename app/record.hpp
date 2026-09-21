#pragma once
#include <array>
#include <bit>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <limits>
#include <unistd.h>

namespace robot_control::app {
enum class RecordKind : std::int64_t { can_rx, can_tx, batch, frame, rejected, cycle, stop, end };
using Packet = std::array<std::int64_t, 32>;
inline constexpr std::int64_t record_magic = 0x31414352; // RCA1, format version1.
static_assert(sizeof(Packet) == 256 && std::endian::native == std::endian::little);

/** Single-owner borrowed nonblocking pipe; one atomic fixed-size write, no retry/allocation/formatting. */
class RecordWriter final {
  public:
    /** Borrow a pipe created and owned by LogSession; that session must outlive this writer. */
    explicit RecordWriter(int fd = -1) noexcept : fd_{fd} {}
    /** Append one timestamped record, preserving syscall errno. Failure latches and never delays control. */
    void append(RecordKind kind, std::array<std::int64_t, 28> fields = {},
                std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now()) noexcept {
        if (failed_)
            return;
        Packet packet{record_magic, sequence_, static_cast<std::int64_t>(kind),
                      std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count()};
        for (std::size_t i = 0; i < fields.size(); ++i)
            packet[i + 4] = fields[i];
        const int saved = errno;
        if (sequence_ == std::numeric_limits<std::int64_t>::max()
            || ::write(fd_, packet.data(), sizeof(packet)) != static_cast<ssize_t>(sizeof(packet)))
            failed_ = true;
        else
            ++sequence_;
        errno = saved;
    }
    /** Return delivery failure without changing the recording sequence or timestamp. */
    [[nodiscard]] bool good() const noexcept {
        return !failed_;
    }

  private:
    int fd_;
    std::int64_t sequence_{0};
    bool failed_{false};
};
} // namespace robot_control::app
