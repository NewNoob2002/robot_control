#include "input/sbus/linux/reader.hpp"

#include <algorithm>
#include <cerrno>
#include <limits>
#include <utility>

namespace robot_control::input::sbus {
using platform::linux::Result;
using platform::linux::Status;
using platform::linux::uart::SerialPort;
using namespace std::chrono_literals;

Status Reader::open(std::string path, const ReaderConfig config) {
    port_ = SerialPort{};
    parser_.reset();
    marker_pending_ = false;
    if (!config.serial.mark_errors || config.maximum_service_gap <= 0ms || config.maximum_service_gap > 1s) {
        return Status::from_errno("SBUS reader config", path, EINVAL);
    }
    config_ = config;
    auto opened = SerialPort::open(std::move(path), config.serial);
    if (!opened.ok())
        return opened.status();
    port_ = std::move(opened).value();
    auto status = reset_stream();
    if (!status.ok())
        port_ = SerialPort{};
    return status;
}

Status Reader::reset_stream() {
    parser_.reset();
    marker_pending_ = false;
    if (session_ == std::numeric_limits<std::uint64_t>::max())
        return Status::from_errno("SBUS session overflow", port_.path(), EOVERFLOW);
    auto status = port_.discard_input();
    if (!status.ok())
        return status;
    ++session_;
    last_service_ = std::chrono::steady_clock::now();
    last_byte_ = last_service_;
    return Status::success();
}

Result<ReadBatch> Reader::fail(Status status) {
    port_ = SerialPort{};
    parser_.reset();
    marker_pending_ = false;
    return Result<ReadBatch>::failure(std::move(status));
}

Result<platform::linux::uart::SerialConfig> Reader::configuration() const noexcept {
    return port_.configuration();
}

Result<ReadBatch> Reader::read(const std::chrono::milliseconds timeout, const int cancellation_fd) {
    if (timeout < 0ms || timeout > 20ms)
        return fail(Status::from_errno("SBUS read timeout", port_.path(), EINVAL));
    ReadBatch batch;
    const auto previous_service = last_service_;
    auto received = port_.read_some(batch.raw, std::min(timeout, config_.maximum_service_gap), cancellation_fd);
    if (!received.ok())
        return fail(received.status());
    batch.captured_at = std::chrono::steady_clock::now();
    batch.raw_size = received.value();
    last_service_ = batch.captured_at;
    auto pending = port_.pending_bytes();
    if (!pending.ok())
        return fail(pending.status());
    if (batch.captured_at - previous_service >= config_.maximum_service_gap) {
        batch.discontinuity = Discontinuity::service_gap;
    } else if (batch.raw_size == batch.raw.size() || pending.value() != 0) {
        // Conservatively reject even a small queue left behind this read.
        batch.discontinuity = Discontinuity::backlog;
    } else if ((marker_pending_ || parser_.statistics().buffered_bytes != 0)
               && batch.captured_at - last_byte_ >= config_.maximum_service_gap) {
        batch.discontinuity = Discontinuity::partial_timeout;
    }
    if (batch.raw_size != 0)
        last_byte_ = batch.captured_at;

    // Decode PARMRK before parsing anything so an error invalidates this batch.
    std::array<std::uint8_t, 256> decoded{};
    std::size_t count = 0;
    if (batch.discontinuity == Discontinuity::none) {
        for (std::size_t index = 0; index < batch.raw_size; ++index) {
            const auto byte = std::to_integer<std::uint8_t>(batch.raw[index]);
            if (marker_pending_) {
                marker_pending_ = false;
                if (byte != 0xff) {
                    // Close immediately; the third error-marker byte may still
                    // be in flight and must never become a new frame header.
                    return fail(Status::from_errno("SBUS parity/framing/break marker", port_.path(), EILSEQ));
                }
                decoded[count++] = 0xff;
            } else if (byte == 0xff) {
                marker_pending_ = true;
            } else {
                decoded[count++] = byte;
            }
        }
    }
    if (batch.discontinuity != Discontinuity::none) {
        auto status = reset_stream();
        if (!status.ok())
            return fail(std::move(status));
    } else {
        auto remaining = std::span{decoded}.first(count);
        while (!remaining.empty()) {
            const auto event = parser_.consume(remaining);
            remaining = remaining.subspan(event.consumed);
            if (event.kind != protocol::EventKind::none)
                batch.events[batch.event_count++] = event;
        }
    }
    batch.session = session_;
    return Result<ReadBatch>::success(batch);
}
} // namespace robot_control::input::sbus
