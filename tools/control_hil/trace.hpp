#pragma once

#include "input/sbus/linux/reader.hpp"

#include <array>
#include <bit>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <chrono>
#include <cstdint>
#include <linux/can.h>
#include <memory>
#include <new>
#include <ostream>

namespace robot_control::hil {
/**
 * Bounded qualification evidence on the sole owner thread. Storage is allocated
 * before device activation, or replaced by a borrowed nonblocking FIFO. Append
 * never allocates or waits; overflow/hard feedback failure invalidates the trial.
 * Selected-wheel stop-band excursions are retained for post-trial review.
 * CAN RX means successful owner MSG_PEEK, not proof of application acceptance.
 */
class Trace final {
  public:
    using Clock = std::chrono::steady_clock;
    enum class Mode : std::uint8_t { input_only, zero, left, right };
    enum class Kind : std::uint8_t { batch, frame, rejected, cycle, can_rx, can_tx, stop, timing, state, end, header };
    using Packet = std::array<std::int64_t, 19>;
    static_assert(std::endian::native == std::endian::little && sizeof(Packet) <= 512);
    static constexpr std::size_t maximum_records = 65536;
    /** One fixed-width record; field meanings are defined in the qualification trace schema. */
    struct Record {
        Kind kind{};
        std::int64_t ns{};
        std::array<std::int64_t, 16> fields{};
    };
    /** Preallocate storage or borrow a zero-only FIFO until dump; owner-only, tolerance0..20 tenths rpm. */
    // Existing capacity argument remains source-compatible; call sites name the validated tolerance.
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
    explicit Trace(Mode mode, std::size_t capacity = maximum_records, int zero_tolerance_tenths_rpm = 0, int stream_fd = -1) noexcept
        : mode_{mode}, capacity_{capacity <= maximum_records ? capacity : 0},
          records_{stream_fd < 0 && capacity_ ? new (std::nothrow) Record[capacity_]{} : nullptr},
          stream_fd_{stream_fd}, overflow_{stream_fd < 0 && !records_},
          zero_tolerance_{zero_tolerance_tenths_rpm} {
        hard_feedback_bad_ = feedback_bad_ =
            zero_tolerance_ < 0 || zero_tolerance_ > 20 || (mode_ == Mode::input_only && zero_tolerance_ != 0);
        if (stream_fd_ >= 0) {
            struct stat info{};
            const int flags = ::fcntl(stream_fd_, F_GETFL);
            overflow_ = mode_ != Mode::zero || flags < 0 || !(flags & O_NONBLOCK)
                        || (flags & O_ACCMODE) != O_WRONLY || ::fstat(stream_fd_, &info) != 0 || !S_ISFIFO(info.st_mode);
            if (!failed())
                append(Kind::header, Clock::now(), {1, zero_tolerance_});
        }
    }
    /** Return a monotonic nanosecond timestamp; not transmitter time or wall time. */
    [[nodiscard]] static std::int64_t ns(Clock::time_point stamp) noexcept {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(stamp.time_since_epoch()).count();
    }
    /** Append to storage or atomically write one FIFO packet; no wait/overwrite, failure is latched. */
    void append(Kind kind, Clock::time_point stamp, std::array<std::int64_t, 16> fields) noexcept {
        if (stream_fd_ >= 0) {
            if (!overflow_ && write_packet(kind, ns(stamp), fields))
                ++count_;
            else
                overflow_ = true;
            return;
        }
        if (count_ == capacity_ || !records_) {
            overflow_ = true;
            return;
        }
        records_[count_++] = {kind, ns(stamp), fields};
    }
    /** Copy every parser event in order; all events retain the batch receive timestamp. */
    void batch(const input::sbus::ReadBatch& value) noexcept {
        if (value.event_count > value.events.size() || value.raw_size > value.raw.size()) {
            overflow_ = true;
            return;
        }
        const auto id = static_cast<std::int64_t>(++batch_id_);
        append(Kind::batch, value.captured_at,
               {id, static_cast<std::int64_t>(value.session), static_cast<std::int64_t>(value.discontinuity),
                static_cast<std::int64_t>(value.raw_size), static_cast<std::int64_t>(value.event_count)});
        for (std::size_t i = 0; i < value.event_count; ++i) {
            const auto& event = value.events[i];
            append(event.kind == input::sbus::protocol::EventKind::frame ? Kind::frame : Kind::rejected,
                   value.captured_at,
                   {id, static_cast<std::int64_t>(value.session), static_cast<std::int64_t>(i), event.frame.channels[0],
                    event.frame.channels[2], event.frame.channels[5], event.frame.channels[6], event.frame.raw_flags,
                    static_cast<std::int64_t>(event.kind)});
        }
    }
    /** Begin the configured feedback envelope after successful bootstrap; never resets evidence. */
    void watch_feedback() noexcept {
        watching_ = true;
    }
    /** Return whether this session is explicitly receive-only; its send wrapper rejects all traffic. */
    [[nodiscard]] bool input_only() const noexcept {
        return mode_ == Mode::input_only;
    }
    /** Return allocation/overflow or hard feedback failure; stop-band review is separate. */
    [[nodiscard]] bool failed() const noexcept {
        return overflow_ || hard_feedback_bad_;
    }
    /** Record a syscall result unchanged; accepted writes are not claimed as bus delivery. */
    // Result then errno follows the syscall observation convention at both wrappers.
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
    void can(Kind kind, const can_frame& frame, Clock::time_point now, std::int64_t result, int error) noexcept {
        std::array<std::int64_t, 16> fields{frame.can_id, frame.can_dlc};
        for (unsigned i = 0; i < 8; ++i)
            fields[2 + i] = frame.data[i];
        fields[10] = result;
        fields[11] = error;
        if (kind == Kind::can_tx && result == CAN_MTU && frame.can_id == 0x201 && frame.can_dlc == 6) {
            if (frame.data[2] || frame.data[3] || frame.data[4] || frame.data[5])
                nonzero_sent_ = true;
            else if (nonzero_sent_)
                stopping_ = true;
        }
        if (!watching_ || kind != Kind::can_rx || frame.can_id != 0x181 || frame.can_dlc != 8) {
            append(kind, now, fields);
            return;
        }
        /** Decode the signed little-endian16-bit feedback without implementation-defined casts. */
        const auto decode = [&](unsigned offset) {
            int value = frame.data[offset] | (static_cast<int>(frame.data[offset + 1]) << 8);
            return value >= 32768 ? value - 65536 : value;
        };
        const int left = decode(4), right = decode(6);
        const int selected = mode_ == Mode::right ? right : left;
        const int other = mode_ == Mode::right ? left : right;
        const bool motion = mode_ == Mode::left || mode_ == Mode::right;
        if (motion ? (other < -zero_tolerance_ || other > zero_tolerance_
                      || (nonzero_sent_ ? (selected < -zero_tolerance_ || selected > 75)
                                        : (selected < -zero_tolerance_ || selected > zero_tolerance_)))
                   : (left < -zero_tolerance_ || left > zero_tolerance_ || right < -zero_tolerance_
                      || right > zero_tolerance_))
        {
            feedback_bad_ = true;
            const bool review = motion && stopping_ && other >= -zero_tolerance_ && other <= zero_tolerance_
                                && selected >= -75 && selected < -zero_tolerance_;
            if (review) {
                fields[12] = 1;
                ++feedback_reviews_;
            } else {
                hard_feedback_bad_ = true;
            }
        }
        append(kind, now, fields);
    }
    /** Export after bounded stop/cleanup; false means output evidence is incomplete. */
    [[nodiscard]] bool dump(std::ostream& output) const {
        if (stream_fd_ >= 0) {
            const bool sent = write_packet(Kind::end, ns(Clock::now()),
                                           {overflow_, hard_feedback_bad_, zero_tolerance_});
            output << "event=stream_end records=" << count_ << " complete=" << (sent && !failed()) << '\n';
            return sent && !failed() && output.good();
        }
        output << "event=trace_header version=1 count=" << count_ << " overflow=" << overflow_
               << " feedback_bad=" << feedback_bad_ << " capacity=" << capacity_
               << " feedback_hard_bad=" << hard_feedback_bad_ << " feedback_reviews=" << feedback_reviews_
               << " zero_feedback_tenths_rpm=" << zero_tolerance_
               << " timestamp=steady_receive_ns storage_bytes=" << sizeof(Record) * capacity_ << '\n';
        for (std::size_t i = 0; i < count_; ++i) {
            const auto& row = records_[i];
            output << "event=trace_row ordinal=" << i << " kind=" << static_cast<unsigned>(row.kind) << " ns=" << row.ns
                   << " fields=";
            for (std::size_t field = 0; field < row.fields.size(); ++field)
                output << (field ? "," : "") << row.fields[field];
            output << '\n';
        }
        output << "event=trace_end count=" << count_ << '\n' << std::flush;
        output << "event=feedback_review_summary count=" << feedback_reviews_
               << " verdict=" << (feedback_reviews_ ? "pending" : "none") << '\n';
        for (std::size_t i = 0; i < count_; ++i) {
            const auto& row = records_[i];
            if (row.kind == Kind::can_rx && row.fields[12] == 1) {
                const auto offset = mode_ == Mode::right ? 8U : 6U;
                const auto raw = row.fields[offset] | (row.fields[offset + 1] << 8);
                output << "event=feedback_review ordinal=" << i << " ns=" << row.ns
                       << " phase=stopping wheel=" << (mode_ == Mode::right ? "right" : "left")
                       << " tenths_rpm=" << (raw >= 32768 ? raw - 65536 : raw)
                       << " tolerance_tenths_rpm=" << zero_tolerance_ << " verdict=pending" << '\n';
            }
        }
        return output.good();
    }

  private:
    /** Write one atomic pipe record; preserve the observed syscall errno and never retry or wait. */
    [[nodiscard]] bool write_packet(Kind kind, std::int64_t stamp,
                                    const std::array<std::int64_t, 16>& fields) const noexcept {
        Packet packet{static_cast<std::int64_t>(count_), static_cast<std::int64_t>(kind), stamp};
        for (std::size_t i = 0; i < fields.size(); ++i)
            packet[i + 3] = fields[i];
        const int saved = errno;
        const auto result = ::write(stream_fd_, packet.data(), sizeof(packet));
        errno = saved;
        return result == static_cast<ssize_t>(sizeof(packet));
    }
    Mode mode_;
    std::size_t capacity_;
    std::unique_ptr<Record[]> records_;
    int stream_fd_{-1}; // Borrowed nonblocking FIFO; caller closes after final export.
    std::size_t count_{0};
    std::uint64_t batch_id_{0};
    bool overflow_{false};
    bool feedback_bad_{false};
    bool hard_feedback_bad_{false};
    bool stopping_{false};
    std::size_t feedback_reviews_{0};
    bool watching_{false};
    bool nonzero_sent_{false};
    int zero_tolerance_{0};
};

/** Bind a borrowed qualification recorder to the syscall ABI on this owner thread; null detaches. */
void bind_trace(Trace* trace) noexcept;
} // namespace robot_control::hil
