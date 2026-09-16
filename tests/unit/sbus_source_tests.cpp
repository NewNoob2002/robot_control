#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string_view>
#include <thread>
#include <vector>
#include "input/sbus/source/source.hpp"

namespace robot_control::input::sbus {
/** Test-only access to unreachable lifetime counter boundaries. */
struct SourceTestAccess {
    /** Place the sequence immediately before exhaustion. */
    static void sequence(Source& source, std::uint64_t value) {
        source.state_.sample.sequence = value;
    }
    /** Place the authorization counter at its terminal boundary. */
    static void authorization(Source& source, std::uint64_t value) {
        source.state_.sample.authorization_generation = value;
    }
};
} // namespace robot_control::input::sbus
namespace {
using namespace robot_control::input::sbus;
using namespace std::chrono_literals;
using Producer = robot_control::input::sbus::Source;
#define CHECK(expression)                                                                                              \
    do {                                                                                                               \
        if (!(expression)) {                                                                                           \
            std::fprintf(stderr, "line %d: %s\n", __LINE__, #expression);                                              \
            std::exit(1);                                                                                              \
        }                                                                                                              \
    } while (false)

/** Explicit offline compatibility fixture, never a deployment calibration. */
SourceConfig profile() {
    SourceConfig config;
    config.steering = {200, 1000, 1800, false};
    config.throttle = config.steering;
    return config;
}
struct FrameValues {
    std::uint16_t button{200};
    std::uint8_t flags{0};
};
/** Create a raw protocol-consistent neutral observation. */
protocol::ParseResult frame(FrameValues values = {}) {
    const auto [button, flags] = values;
    protocol::ParseResult result;
    result.kind = protocol::EventKind::frame;
    result.frame.channels.fill(1000);
    result.frame.channels[5] = button;
    result.frame.raw_flags = flags;
    result.frame.frame_lost = (flags & 4U) != 0;
    result.frame.failsafe = (flags & 8U) != 0;
    result.frame.digital_channel_17 = (flags & 1U) != 0;
    result.frame.digital_channel_18 = (flags & 2U) != 0;
    return result;
}
/** Test driver with explicitly advanced injected time. */
struct Driver {
    Producer source;
    MonotonicTime now{1s};
    std::uint64_t session{1};
    /** Inject a complete offline profile. */
    explicit Driver(SourceConfig config = profile()) : source(config) {}
    /** Deliver one event after the requested fake interval. */
    SourceSnapshot send(protocol::ParseResult event = frame(), Duration delay = 7ms) {
        now += delay;
        return source.consume({std::span{&event, 1}, now, session, false}, now);
    }
    /** Recover communication without authorizing input. */
    void recover() {
        CHECK(!send().sample.valid);
        CHECK(!send().sample.valid);
        CHECK(send().health == Health::disabled);
    }
    /** Enable from neutral and verify the initial zero publication. */
    void enable() {
        recover();
        const auto s = send(frame({.button = 1800}));
        CHECK(s.sample.valid && s.sample.enabled && !s.sample.command.stop_requested);
        CHECK(s.sample.command.left_rpm == 0 && s.sample.command.right_rpm == 0);
        CHECK(s.sample.authorization_generation == 1);
    }
};
/** Reject invalid configuration instead of silently clamping it. */
void configuration() {
    CHECK(validate(profile()) == ConfigError::none);
    CHECK(validate(SourceConfig{}) == ConfigError::calibration);
    auto bad = profile();
    bad.channels[1] = bad.channels[0];
    CHECK(validate(bad) == ConfigError::channels);
    bad = profile();
    bad.channels[3] = 16;
    CHECK(validate(bad) == ConfigError::channels);
    bad = profile();
    bad.throttle.maximum = 2048;
    CHECK(validate(bad) == ConfigError::calibration);
    bad = profile();
    bad.steering.center = 200;
    CHECK(validate(bad) == ConfigError::calibration);
    bad = profile();
    bad.deadband = 1000;
    CHECK(validate(bad) == ConfigError::deadband);
    bad = profile();
    bad.deadband = -1;
    CHECK(validate(bad) == ConfigError::deadband);
    bad = profile();
    bad.button_press = bad.button_release;
    CHECK(validate(bad) == ConfigError::thresholds);
    bad = profile();
    bad.gear_high = 2048;
    CHECK(validate(bad) == ConfigError::thresholds);
    bad = profile();
    bad.gear_rpm[1] = 101;
    CHECK(validate(bad) == ConfigError::limits);
    bad = profile();
    bad.output_deadband_rpm = -1;
    CHECK(validate(bad) == ConfigError::limits);
    bad = profile();
    bad.recovery_frames = 0;
    CHECK(validate(bad) == ConfigError::timing);
    bad = profile();
    bad.timeout = 0ms;
    CHECK(validate(bad) == ConfigError::timing);
    bad = profile();
    bad.button_cooldown = -1ms;
    CHECK(validate(bad) == ConfigError::timing);
    Driver invalid{SourceConfig{}};
    for (int i = 0; i < 6; ++i)
        CHECK(!invalid.send(frame({.button = static_cast<std::uint16_t>(i % 2 == 0 ? 200 : 1800)})).sample.valid);
    CHECK(invalid.source.snapshot().fault == InputFault::configuration);
}
/** V09-V13: independently specified normalization, mixing and neutral rearm. */
void mapping() {
    constexpr std::array<std::uint16_t, 11> raw{0, 200, 600, 1000, 1400, 1800, 2047, 959, 960, 1040, 1041};
    constexpr std::array<std::int32_t, 11> expected{-1000, -1000, -500, 0, 500, 1000, 1000, -51, 0, 0, 51};
    for (std::size_t i = 0; i < raw.size(); ++i) {
        Driver d;
        auto f = frame();
        f.frame.channels[2] = raw[i];
        CHECK(d.send(f).throttle == expected[i]);
        auto config = profile();
        config.throttle.reversed = true;
        Driver reverse{config};
        CHECK(reverse.send(f).throttle == -expected[i]);
    }
    Driver d;
    d.enable();
    auto f = frame({.button = 1800});
    f.frame.channels[2] = 1400;
    f.frame.channels[0] = 1200;
    auto s = d.send(f);
    CHECK(s.sample.command.left_rpm == 45 && s.sample.command.right_rpm == 15);
    f.frame.channels[2] = f.frame.channels[0] = 1800;
    f.frame.channels[6] = 1800;
    s = d.send(f);
    CHECK(s.sample.command.left_rpm == 100 && s.sample.command.right_rpm == 0);
    f.frame.channels[2] = 1000;
    s = d.send(f);
    CHECK(s.sample.command.left_rpm == 100 && s.sample.command.right_rpm == -100);
    Driver nonneutral;
    nonneutral.recover();
    f = frame({.button = 1800});
    f.frame.channels[2] = 1041;
    f.frame.channels[6] = 200;
    s = nonneutral.send(f);
    CHECK(s.throttle == 51 && s.candidate.left_rpm == 0 && !s.sample.enabled);
    CHECK(!nonneutral.send(frame({.button = 1800})).sample.enabled);
    CHECK(!nonneutral.send(frame({.button = 1000})).sample.enabled);
    CHECK(!nonneutral.send().sample.enabled);
    CHECK(nonneutral.send(frame({.button = 1800})).sample.enabled);
    auto config = profile();
    config.gear_rpm.fill(std::numeric_limits<std::int32_t>::max());
    config.maximum_rpm = std::numeric_limits<std::int32_t>::max();
    Driver wide{config};
    wide.enable();
    f = frame({.button = 1800});
    f.frame.channels[2] = 1800;
    CHECK(wide.send(f).sample.command.left_rpm == std::numeric_limits<std::int32_t>::max());
}
/** V07: timeout equality, future times and immutable repeated reads. */
void time_boundaries() {
    for (const auto age : {99ms, 100ms, 101ms}) {
        Driver d;
        d.enable();
        const auto before = d.source.snapshot();
        const auto result = d.source.tick(d.now + age);
        CHECK(result.sample.valid == (age < 100ms));
        CHECK(result.sample.captured_at == before.sample.captured_at);
        CHECK((result.sample.sequence == before.sample.sequence) == (age < 100ms));
        CHECK(d.source.snapshot().sample.sequence == result.sample.sequence);
        if (age >= 100ms)
            CHECK(result.sample.command.left_rpm == 0 && result.sample.command.stop_requested);
    }
    Driver d;
    d.enable();
    auto f = frame();
    CHECK(d.source.consume({std::span{&f, 1}, d.now + 1ms, 1, false}, d.now).fault == InputFault::future_time);
    Driver backward;
    backward.enable();
    CHECK(backward.source.tick(backward.now - 1ms).fault == InputFault::clock_regression);
    Driver stale;
    stale.enable();
    stale.now += 100ms;
    auto s = stale.send(frame({.button = 1800}), 0ms);
    CHECK(!s.sample.valid && s.last_fault == InputFault::timeout);
}
/** V06/V08: lost and rejected events survive later healthy frames in the same read. */
void recovery_and_batches() {
    auto config = profile();
    config.button_cooldown = 0ms;
    Driver d{config};
    CHECK(d.send(frame({.button = 1800})).health == Health::recovering);
    CHECK(d.send(frame({.button = 1800})).health == Health::recovering);
    CHECK(d.send(frame({.button = 1800})).health == Health::disabled);
    CHECK(!d.send(frame({.button = 1800})).sample.valid);
    CHECK(!d.send().sample.valid);
    CHECK(d.send(frame({.button = 1800})).sample.valid);
    for (const auto flags : {4U, 8U, 12U}) {
        std::array events{frame({.flags = static_cast<std::uint8_t>(flags)}), frame(), frame(), frame(),
                          frame({.button = 1800})};
        d.now += 7ms;
        auto s = d.source.consume({events, d.now, d.session, false}, d.now);
        CHECK(!s.sample.valid && s.sample.command.left_rpm == 0 && s.sample.command.right_rpm == 0);
        CHECK(s.last_fault == ((flags & 8U) != 0 ? InputFault::failsafe : InputFault::frame_lost));
        CHECK(s.recovery_count == 0);
        CHECK(s.last_fault_flags == flags);
        d.recover();
        CHECK(d.send(frame({.button = 1800})).sample.valid);
    }
    Driver coalesced;
    std::array healthy{frame(), frame(), frame()};
    auto s = coalesced.source.consume({healthy, coalesced.now, 1, false}, coalesced.now);
    CHECK(s.recovery_count == 1 && !s.sample.valid);
    CHECK(coalesced.send().recovery_count == 2);
    CHECK(coalesced.send().health == Health::disabled);
    std::array rearm{frame({.button = 1800}), frame({.button = 1800})};
    rearm[1].frame.channels[2] = 1800;
    coalesced.now += 7ms;
    s = coalesced.source.consume({rearm, coalesced.now, 1, false}, coalesced.now);
    CHECK(s.sample.valid && s.sample.command.left_rpm == 0);
    CHECK(coalesced.send(rearm[1]).sample.command.left_rpm == 60);
    auto rejected = frame();
    rejected.kind = protocol::EventKind::rejected;
    CHECK(!coalesced.send(rejected).sample.valid);
    CHECK(coalesced.send().recovery_count == 1);
    CHECK(coalesced.send(rejected).recovery_count == 0);
}
/** V14: exact cooldown boundary, consumed early edges and unconditional fault stop. */
void cooldown() {
    for (const auto interval : {299ms, 300ms}) {
        Driver d;
        d.enable();
        const auto enabled_at = d.now;
        for (int i = 0; i < 5; ++i)
            static_cast<void>(d.send(frame(), 50ms));
        const auto s = d.send(frame({.button = 1800}), interval - (d.now - enabled_at));
        CHECK(s.sample.enabled == (interval < 300ms));
        CHECK(!d.send(frame({.button = 1800, .flags = 4})).sample.valid);
    }
    Driver d;
    d.enable();
    static_cast<void>(d.send());
    CHECK(d.send(frame({.button = 1800})).sample.enabled);
    for (int i = 0; i < 8; ++i)
        CHECK(d.send(frame({.button = 1800}), 50ms).sample.enabled);
    static_cast<void>(d.send());
    CHECK(!d.send(frame({.button = 1800})).sample.enabled);
}
/** V15: replay, reopen, discontinuity, errors and checked counter exhaustion. */
void identities() {
    Driver d;
    d.enable();
    auto f = frame();
    auto s = d.source.consume({std::span{&f, 1}, d.now, 1, false}, d.now);
    CHECK(!s.sample.valid && s.fault == InputFault::replay);
    d.session = 2;
    CHECK(!d.send(frame({.button = 1800})).sample.valid);
    CHECK(d.source.snapshot().sample.session_generation == 2);
    d.session = 1;
    CHECK(d.send().fault == InputFault::replay);
    CHECK(d.source.snapshot().sample.session_generation == 2);
    auto reconnect_profile = profile();
    reconnect_profile.button_cooldown = 0ms;
    Driver transport{reconnect_profile};
    transport.enable();
    CHECK(transport.source.transport_error(transport.now).fault == InputFault::transport);
    CHECK(!transport.send().sample.valid);
    ++transport.session;
    transport.recover();
    CHECK(transport.send(frame({.button = 1800})).sample.valid);
    transport.now += 7ms;
    CHECK(!transport.source.consume({{}, transport.now, ++transport.session, true}, transport.now).sample.valid);
    Driver sequence;
    sequence.enable();
    SourceTestAccess::sequence(sequence.source, std::numeric_limits<std::uint64_t>::max() - 1);
    s = sequence.send();
    CHECK(s.fault == InputFault::counter_exhausted && !s.sample.valid);
    CHECK(s.sample.sequence == std::numeric_limits<std::uint64_t>::max());
    CHECK(sequence.send().sample.sequence == s.sample.sequence);
    Driver authorization;
    authorization.recover();
    SourceTestAccess::authorization(authorization.source, std::numeric_limits<std::uint64_t>::max());
    CHECK(authorization.send(frame({.button = 1800})).fault == InputFault::counter_exhausted);
    Driver max_session;
    max_session.session = std::numeric_limits<std::uint64_t>::max();
    max_session.recover();
    max_session.session = 0;
    CHECK(!max_session.send().sample.valid);
    Driver shutdown;
    shutdown.enable();
    auto stopped = shutdown.source.stop(shutdown.now);
    CHECK(stopped.fault == InputFault::shutdown && !stopped.sample.valid);
    CHECK(stopped.sample.command.left_rpm == 0 && stopped.sample.command.stop_requested);
    CHECK(!shutdown.send().sample.valid);
    Driver malformed;
    auto invalid = frame();
    invalid.frame.channels[0] = 2048;
    CHECK(malformed.send(invalid).fault == InputFault::malformed);
    invalid = frame();
    invalid.frame.raw_flags = 4;
    CHECK(malformed.send(invalid).fault == InputFault::malformed);
}

/** Cover asymmetric axes, exact gear/button/output boundaries and invalid event envelopes. */
void additional_boundaries() {
    auto config = profile();
    config.throttle = {100, 900, 1900, false};
    config.steering.reversed = true;
    Driver asymmetric{config};
    auto event = frame();
    event.frame.channels[2] = 1400;
    event.frame.channels[0] = 1400;
    auto s = asymmetric.send(event);
    CHECK(s.throttle == 500 && s.steering == -500);
    event.frame.channels[2] = 500;
    CHECK(asymmetric.send(event).throttle == -500);
    Driver d;
    d.enable();
    event = frame({.button = 1800});
    event.frame.channels[2] = 1800;
    constexpr std::array<std::uint16_t, 4> gears{500, 501, 1499, 1500};
    constexpr std::array<std::int32_t, 4> speeds{30, 60, 60, 100};
    for (std::size_t i = 0; i < gears.size(); ++i) {
        event.frame.channels[6] = gears[i];
        CHECK(d.send(event).sample.command.left_rpm == speeds[i]);
    }
    event.frame.channels[6] = 500;
    event.frame.channels[2] = 1080;
    CHECK(d.send(event).sample.command.left_rpm == 3);
    event.frame.channels[2] = 1079;
    CHECK(d.send(event).sample.command.left_rpm == 0);
    Driver buttons;
    buttons.recover();
    CHECK(!buttons.send(frame({.button = 1499})).sample.enabled);
    CHECK(buttons.send(frame({.button = 1500})).sample.enabled);
    Driver empty;
    empty.enable();
    const auto captured = empty.source.snapshot().sample.captured_at;
    empty.now += 99ms;
    s = empty.source.consume({{}, empty.now, 1, false}, empty.now);
    CHECK(s.sample.valid && s.sample.captured_at == captured);
    empty.now += 1ms;
    CHECK(!empty.source.consume({{}, empty.now, 1, false}, empty.now).sample.valid);
    Driver envelope;
    std::array<protocol::ParseResult, 257> excessive{};
    CHECK(envelope.source.consume({excessive, envelope.now, 1, false}, envelope.now).fault == InputFault::malformed);
    event = frame();
    event.kind = protocol::EventKind::none;
    CHECK(envelope.send(event).fault == InputFault::malformed);
    Driver old;
    old.enable();
    const auto timestamp = old.now;
    old.now += 100ms;
    event = frame();
    CHECK(old.source.consume({std::span{&event, 1}, timestamp, 1, false}, old.now).fault == InputFault::timeout);
    Driver sequence;
    const auto before = sequence.send();
    const auto after = sequence.source.tick(sequence.now);
    CHECK(before.sample.sequence == after.sample.sequence && before.recovery_count == after.recovery_count);
    Driver restart;
    restart.enable();
    ++restart.session;
    CHECK(!restart.send(frame({.button = 1800})).sample.valid);
    CHECK(!restart.send(frame({.button = 1800})).sample.valid);
    CHECK(!restart.send(frame({.button = 1800})).sample.valid);
    CHECK(restart.source.snapshot().sample.authorization_generation == 1);
}

/** Replay archived ordered parser observations using original receive timestamps. */
void replay() {
    Producer source{profile()};
    std::int64_t timestamp = 0;
    std::uint64_t session = 0;
    std::size_t count = 0;
    std::size_t frames = 0;
    std::size_t rejects = 0;
    std::size_t lost = 0;
    std::size_t failsafe = 0;
    while (std::cin >> timestamp >> session >> count) {
        CHECK(timestamp >= 0 && count <= 256);
        std::vector<protocol::ParseResult> events(count);
        bool fault_in_read = false;
        for (auto& event : events) {
            char kind = 0;
            CHECK(static_cast<bool>(std::cin >> kind));
            if (kind == 'r') {
                event.kind = protocol::EventKind::rejected;
                fault_in_read = true;
                ++rejects;
            } else {
                CHECK(kind == 'f');
                unsigned int flags = 0;
                CHECK(static_cast<bool>(std::cin >> flags) && flags <= 255);
                event = frame({.flags = static_cast<std::uint8_t>(flags)});
                for (auto& channel : event.frame.channels)
                    CHECK(static_cast<bool>(std::cin >> channel) && channel <= 2047);
                ++frames;
                if (event.frame.frame_lost)
                    ++lost;
                if (event.frame.failsafe)
                    ++failsafe;
                fault_in_read = fault_in_read || event.frame.frame_lost || event.frame.failsafe;
            }
        }
        const MonotonicTime received{std::chrono::duration_cast<Duration>(std::chrono::nanoseconds{timestamp})};
        const auto state = source.consume({events, received, session, false}, received);
        CHECK(state.fault != InputFault::replay && state.fault != InputFault::future_time);
        CHECK(!state.sample.valid
              || robot_control::domain::command::structurally_valid(state.sample,
                                                                    robot_control::domain::command::Source::sbus));
        CHECK(state.sample.valid || (state.sample.command.left_rpm == 0 && state.sample.command.right_rpm == 0));
        if (fault_in_read)
            CHECK(!state.sample.valid && state.recovery_count == 0);
        if (!events.empty())
            CHECK(state.sample.captured_at <= received);
    }
    CHECK(std::cin.eof());
    std::cout << frames << ' ' << rejects << ' ' << lost << ' ' << failsafe << '\n';
}

/** Concurrent value readers must never observe mixed fields or regressing revisions. */
void concurrent_snapshots() {
    Driver d;
    d.enable();
    std::atomic<bool> done{false};
    std::thread consumer([&] {
        std::uint64_t last = 0;
        while (!done.load()) {
            const auto s = d.source.snapshot();
            CHECK(s.sample.sequence >= last);
            last = s.sample.sequence;
            CHECK(s.sample.source == robot_control::domain::command::Source::sbus);
            CHECK(!s.sample.valid
                  || robot_control::domain::command::structurally_valid(s.sample,
                                                                        robot_control::domain::command::Source::sbus));
            CHECK(s.sample.valid || (s.sample.command.left_rpm == 0 && s.sample.command.right_rpm == 0));
        }
    });
    for (int i = 0; i < 1000; ++i) {
        auto f = frame({.button = 1800});
        f.frame.channels[2] = i % 2 == 0 ? 1800 : 200;
        static_cast<void>(d.send(f));
    }
    done.store(true);
    consumer.join();
    auto copy = d.source.snapshot();
    copy.sample.sequence = 0;
    CHECK(d.source.snapshot().sample.sequence != copy.sample.sequence);
}
} // namespace
/** Run deterministic input contracts without devices or a live clock. */
int main(int argc, char* argv[]) {
    if (argc == 2 && std::string_view{argv[1]} == "--replay") {
        replay();
        return 0;
    }
    CHECK(argc == 1);
    additional_boundaries();
    configuration();
    mapping();
    time_boundaries();
    recovery_and_batches();
    cooldown();
    identities();
    concurrent_snapshots();
    std::puts("P9.3 source contract: PASS");
}
