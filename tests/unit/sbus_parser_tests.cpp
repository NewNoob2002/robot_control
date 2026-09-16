#include "input/sbus/protocol/parser.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <iostream>
#include <span>
#include <string_view>
#include <vector>

namespace {
using namespace robot_control::input::sbus::protocol;
using Bytes = std::array<std::uint8_t, 25>;
int failures = 0;

/** Record a failed contract assertion without disabling Release checks. */
void check(bool condition, std::string_view expression, int line) {
    if (!condition) {
        ++failures;
        std::cerr << "line=" << line << " FAIL " << expression << '\n';
    }
}
#define CHECK(expression) check((expression), #expression, __LINE__)

/** Return the independent all-zero channel frame specified by V01. */
Bytes zero_frame() {
    Bytes bytes{};
    bytes[0] = 0x0f;
    return bytes;
}

/** Drain every event from a chunk, checking progress and fixed storage bounds. */
std::vector<ParseResult> drain(Parser& parser, std::span<const std::uint8_t> bytes) {
    std::vector<ParseResult> events;
    while (!bytes.empty()) {
        const auto result = parser.consume(bytes);
        CHECK(result.consumed > 0 && result.consumed <= bytes.size());
        CHECK(parser.statistics().buffered_bytes < 25);
        if (result.consumed == 0 || result.consumed > bytes.size()) {
            break;
        }
        if (result.kind != EventKind::none) {
            events.push_back(result);
        }
        bytes = bytes.subspan(result.consumed);
    }
    return events;
}

/** Verify V01/V02/V03 and a literal mixed-channel vector from the legacy tests. */
void decode_vectors() {
    Parser parser;
    auto bytes = zero_frame();
    auto result = parser.consume(bytes);
    CHECK(result.kind == EventKind::frame && result.consumed == 25);
    CHECK(result.frame.channels == (std::array<std::uint16_t, 16>{}));
    CHECK(result.frame.raw_flags == 0);
    CHECK(!result.frame.digital_channel_17 && !result.frame.digital_channel_18);
    CHECK(!result.frame.frame_lost && !result.frame.failsafe);
    std::fill(bytes.begin() + 1, bytes.begin() + 23, 0xff);
    bytes[23] = 0x0f;
    result = parser.consume(bytes);
    CHECK(result.kind == EventKind::frame);
    CHECK(std::all_of(result.frame.channels.begin(), result.frame.channels.end(), [](auto value) {
        return value == 2047;
    }));
    CHECK(result.frame.digital_channel_17 && result.frame.digital_channel_18);
    CHECK(result.frame.frame_lost && result.frame.failsafe);

    for (std::size_t bit = 0; bit < 176; ++bit) {
        bytes = zero_frame();
        bytes[1 + bit / 8] = static_cast<std::uint8_t>(1U << (bit % 8));
        result = parser.consume(bytes);
        CHECK(result.kind == EventKind::frame);
        for (std::size_t channel = 0; channel < 16; ++channel) {
            CHECK(result.frame.channels[channel] == (channel == bit / 11 ? 1U << (bit % 11) : 0U));
        }
    }

    constexpr Bytes mixed{0x0f, 0x00, 0x08, 0x00, 0x32, 0xe8, 0x83, 0xbe, 0xff, 0xc1, 0x92, 0xbb, 0x08,
                          0xff, 0x7f, 0x04, 0x42, 0x10, 0x84, 0x40, 0x04, 0x24, 0x40, 0x03, 0x00};
    constexpr std::array<std::uint16_t, 16> expected{0,    1,    200, 500, 1000, 1023, 1200, 1500,
                                                     1800, 2047, 17,  33,  65,   129,  257,  513};
    result = parser.consume(mixed);
    CHECK(result.kind == EventKind::frame && result.frame.channels == expected);
    CHECK(result.frame.raw_flags == 3);
}

/** Verify V04 chunk independence, empty input and adjacent frame delivery. */
void chunks() {
    const auto bytes = zero_frame();
    for (std::size_t split = 1; split < bytes.size(); ++split) {
        Parser parser;
        auto result = parser.consume(std::span{bytes}.first(split));
        CHECK(result.kind == EventKind::none && result.consumed == split);
        CHECK(parser.statistics().buffered_bytes == split);
        result = parser.consume({});
        CHECK(result.kind == EventKind::none && result.consumed == 0);
        CHECK(parser.statistics().buffered_bytes == split);
        result = parser.consume(std::span{bytes}.subspan(split));
        CHECK(result.kind == EventKind::frame && result.consumed == 25 - split);
        CHECK(parser.statistics().frames == 1 && parser.statistics().bytes_consumed == 25);
    }
    std::vector<std::uint8_t> stream(bytes.begin(), bytes.end());
    stream.insert(stream.end(), bytes.begin(), bytes.end());
    for (std::size_t chunk = 1; chunk <= stream.size(); ++chunk) {
        Parser parser;
        std::size_t frames = 0;
        for (std::size_t offset = 0; offset < stream.size(); offset += chunk) {
            const auto events =
                drain(parser, std::span{stream}.subspan(offset, std::min(chunk, stream.size() - offset)));
            for (const auto& event : events) {
                CHECK(event.kind == EventKind::frame);
                CHECK(event.frame.channels == (std::array<std::uint16_t, 16>{}));
            }
            frames += events.size();
        }
        CHECK(frames == 2 && parser.statistics().bytes_consumed == 50);
    }
}

/** Verify V05 rejection, all suffix offsets, embedded headers and reset isolation. */
void resynchronization() {
    const auto valid = zero_frame();
    Parser parser;
    auto bad = valid;
    bad[24] = 0x04; // Unsupported footer must not silently select another profile.
    auto events = drain(parser, bad);
    CHECK(events.size() == 1);
    if (!events.empty()) {
        CHECK(events[0].kind == EventKind::rejected);
        CHECK(events[0].frame.channels == (std::array<std::uint16_t, 16>{}));
    }
    CHECK(parser.statistics().rejected_frames == 1 && parser.statistics().discarded_bytes == 25);
    events = drain(parser, valid);
    CHECK(events.size() == 1 && parser.statistics().frames == 1);

    // Each prefix leaves the next real header at a different retained offset.
    auto nonzero = valid;
    std::fill(nonzero.begin() + 1, nonzero.begin() + 24, 0xff);
    for (std::size_t prefix = 1; prefix < 25; ++prefix) {
        parser.reset();
        std::vector<std::uint8_t> stream(prefix, 0x55);
        stream[0] = 0x0f;
        stream.insert(stream.end(), nonzero.begin(), nonzero.end());
        events = drain(parser, stream);
        CHECK(events.size() == 2);
        if (events.size() == 2) {
            CHECK(events[0].kind == EventKind::rejected && events[1].kind == EventKind::frame);
            CHECK(events[1].frame.channels[15] == 2047 && events[1].frame.raw_flags == 255);
        }
        CHECK(parser.statistics().discarded_bytes == prefix);
    }

    parser.reset();
    auto embedded = valid;
    embedded[1] = 0x0f;
    embedded[10] = 0x0f;
    events = drain(parser, embedded);
    CHECK(events.size() == 1 && parser.statistics().rejected_frames == 0);
    if (!events.empty()) {
        CHECK(events[0].frame.channels[0] == 15);
    }
    // Reject then retain the earliest of two possible headers, not the last.
    std::vector<std::uint8_t> multiple{0x0f, 0x55, 0x55};
    embedded[21] = 0x55;
    multiple.insert(multiple.end(), embedded.begin(), embedded.end());
    parser.reset();
    events = drain(parser, multiple);
    CHECK(events.size() == 2 && parser.statistics().discarded_bytes == 3);
    if (events.size() == 2) {
        CHECK(events[0].kind == EventKind::rejected && events[1].kind == EventKind::frame);
        CHECK(events[1].frame.channels[0] == 15);
    }

    parser.reset();
    CHECK(parser.consume(std::span{valid}.first(10)).kind == EventKind::none);
    parser.reset();
    CHECK(parser.statistics().bytes_consumed == 0 && parser.statistics().buffered_bytes == 0);
    CHECK(parser.consume(std::span{valid}.subspan(10)).kind == EventKind::none);
    CHECK(parser.statistics().discarded_bytes == 15 && parser.statistics().frames == 0);
    CHECK(parser.consume(valid).kind == EventKind::frame);
}

/** Verify V06 raw flag preservation and ordered lost/rejected/healthy events. */
void flags_and_order() {
    Parser parser;
    for (unsigned int bit = 0; bit < 8; ++bit) {
        auto bytes = zero_frame();
        bytes[23] = static_cast<std::uint8_t>(1U << bit);
        const auto result = parser.consume(bytes);
        CHECK(result.kind == EventKind::frame && result.frame.raw_flags == bytes[23]);
        CHECK(result.frame.digital_channel_17 == (bit == 0));
        CHECK(result.frame.digital_channel_18 == (bit == 1));
        CHECK(result.frame.frame_lost == (bit == 2));
        CHECK(result.frame.failsafe == (bit == 3));
        CHECK(result.frame.channels == (std::array<std::uint16_t, 16>{}));
    }
    CHECK(parser.statistics().frames == 8);
    CHECK(parser.statistics().lost_frames == 1 && parser.statistics().failsafe_frames == 1);
    parser.reset();
    std::vector<std::uint8_t> stream;
    for (const auto flag : {4, 8, 0}) {
        auto bytes = zero_frame();
        bytes[23] = static_cast<std::uint8_t>(flag);
        stream.insert(stream.end(), bytes.begin(), bytes.end());
    }
    const auto events = drain(parser, stream);
    CHECK(events.size() == 3);
    if (events.size() == 3) {
        CHECK(events[0].frame.frame_lost && events[1].frame.failsafe);
        CHECK(!events[2].frame.frame_lost && !events[2].frame.failsafe);
    }
    CHECK(parser.statistics().bytes_consumed == 75 && parser.statistics().discarded_bytes == 0);
}

/** Exercise long noise, repeated false headers, and fixed parser storage. */
void bounded_noise() {
    static_assert(sizeof(Parser) < 256);
    Parser parser;
    std::array<std::uint8_t, 4096> noise{};
    noise.fill(0x55);
    for (int repeat = 0; repeat < 256; ++repeat) {
        CHECK(drain(parser, noise).empty());
    }
    CHECK(parser.statistics().bytes_consumed == 1048576);
    CHECK(parser.statistics().discarded_bytes == 1048576);
    noise.fill(0x0f);
    for (int repeat = 0; repeat < 8; ++repeat) {
        const auto events = drain(parser, noise);
        CHECK(std::all_of(events.begin(), events.end(), [](const auto& event) {
            return event.kind == EventKind::rejected;
        }));
    }
    CHECK(parser.statistics().frames == 0 && parser.statistics().buffered_bytes == 24);
    parser.reset();
    CHECK(parser.consume(zero_frame()).kind == EventKind::frame);
}
} // namespace

/** Run the pure SBUS contract checks; return failure if any assertion fails. */
int main() {
    decode_vectors();
    chunks();
    resynchronization();
    flags_and_order();
    bounded_noise();
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
