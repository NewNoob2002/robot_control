#include "input/sbus/protocol/parser.hpp"

namespace robot_control::input::sbus::protocol {

ParseResult Parser::consume(const std::span<const std::uint8_t> bytes) noexcept {
    ParseResult result{};
    for (const auto byte : bytes) {
        ++result.consumed;
        ++statistics_.bytes_consumed;
        if (length_ == 0 && byte != 0x0f) {
            ++statistics_.discarded_bytes;
            continue;
        }
        buffer_[length_++] = byte;
        if (length_ != buffer_.size()) {
            continue;
        }
        if (buffer_.back() != 0x00) {
            ++statistics_.rejected_frames;
            resynchronize();
            result.kind = EventKind::rejected;
            return result;
        }
        result.frame = decode();
        result.kind = EventKind::frame;
        ++statistics_.frames;
        statistics_.lost_frames += result.frame.frame_lost ? 1U : 0U;
        statistics_.failsafe_frames += result.frame.failsafe ? 1U : 0U;
        length_ = 0;
        return result;
    }
    return result;
}

Frame Parser::decode() const noexcept {
    Frame frame{};
    for (std::size_t channel = 0; channel < frame.channels.size(); ++channel) {
        const auto bit = channel * 11;
        const auto offset = 1 + bit / 8;
        // The final window includes flags, whose bits are removed by the mask.
        const auto packed = static_cast<std::uint32_t>(buffer_[offset])
                            | (static_cast<std::uint32_t>(buffer_[offset + 1]) << 8U)
                            | (static_cast<std::uint32_t>(buffer_[offset + 2]) << 16U);
        frame.channels[channel] = static_cast<std::uint16_t>((packed >> (bit % 8)) & 0x07ffU);
    }
    frame.raw_flags = buffer_[23];
    frame.digital_channel_17 = (frame.raw_flags & 0x01U) != 0;
    frame.digital_channel_18 = (frame.raw_flags & 0x02U) != 0;
    frame.frame_lost = (frame.raw_flags & 0x04U) != 0;
    frame.failsafe = (frame.raw_flags & 0x08U) != 0;
    return frame;
}

void Parser::resynchronize() noexcept {
    std::size_t offset = 1;
    while (offset < length_ && buffer_[offset] != 0x0f) {
        ++offset;
    }
    statistics_.discarded_bytes += offset;
    length_ -= offset;
    // Forward copying is safe here because the destination precedes the source.
    for (std::size_t index = 0; index < length_; ++index) {
        buffer_[index] = buffer_[offset + index];
    }
}

void Parser::reset() noexcept {
    *this = Parser{};
}

Statistics Parser::statistics() const noexcept {
    auto result = statistics_;
    result.buffered_bytes = length_;
    return result;
}

} // namespace robot_control::input::sbus::protocol
