#include "input/sbus/linux/reader.hpp"
#include "input/sbus/linux/source_bridge.hpp"

#include <fcntl.h>
#include <sys/eventfd.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <span>
#include <thread>

namespace {
using namespace std::chrono_literals;
using namespace robot_control::input::sbus;
using robot_control::platform::linux::UniqueFd;
namespace uart = robot_control::platform::linux::uart;

#define CHECK(expression)                                                                                              \
    do {                                                                                                               \
        if (!(expression)) {                                                                                           \
            std::fprintf(stderr, "line %d: %s\n", __LINE__, #expression);                                              \
            std::exit(1);                                                                                              \
        }                                                                                                              \
    } while (false)

std::span<const std::uint8_t> injected{};

/** Own a test-only PTY pair and retain the slave while reopening the reader. */
struct Pty {
    UniqueFd master;
    UniqueFd slave;
    std::array<char, 256> path{};
    /** Create an isolated PTY; no physical device is accessed. */
    Pty() : master{::posix_openpt(O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC)} {
        CHECK(master.get() >= 0);
        CHECK(::grantpt(master.get()) == 0);
        CHECK(::unlockpt(master.get()) == 0);
        CHECK(::ptsname_r(master.get(), path.data(), path.size()) == 0);
        slave.reset(::open(path.data(), O_RDONLY | O_NOCTTY | O_NONBLOCK | O_CLOEXEC));
        CHECK(slave.get() >= 0);
    }
    /** Write one bounded chunk of synthetic wire data. */
    void send(std::span<const std::uint8_t> data) const {
        CHECK(::write(master.get(), data.data(), data.size()) == static_cast<ssize_t>(data.size()));
    }
};

/** Explicit 8N2 PTY profile; never silently substitute it for production 8E2. */
ReaderConfig pty_config() {
    ReaderConfig config;
    config.serial.even_parity = false;
    config.maximum_service_gap = 1000ms;
    return config;
}

/** Consume one successful bounded observation by value. */
ReadBatch receive(Reader& reader, std::chrono::milliseconds timeout = 20ms) {
    auto result = reader.read(timeout);
    CHECK(result.ok());
    return std::move(result).value();
}

/** Check exact rate/readback, unsupported parity, errors, queue flush and close. */
void test_uart() {
    Pty pty;
    auto profile = pty_config().serial;
    auto opened = uart::SerialPort::open(pty.path.data(), profile);
    CHECK(opened.ok());
    auto& port = opened.value();
    const auto actual = port.configuration();
    CHECK(actual.ok());
    CHECK(actual.value().baud_rate == uart::BaudRate::baud_100000);
    CHECK(actual.value().two_stop_bits && !actual.value().even_parity && actual.value().mark_errors);
    const std::array<std::uint8_t, 3> junk{1, 2, 3};
    pty.send(junk);
    std::this_thread::sleep_for(2ms);
    CHECK(port.pending_bytes().ok() && port.pending_bytes().value() == 3);
    CHECK(port.discard_input().ok());
    CHECK(port.pending_bytes().ok() && port.pending_bytes().value() == 0);
    profile.even_parity = true;
    const auto unsupported = uart::SerialPort::open(pty.path.data(), profile);
    CHECK(!unsupported.ok()); // Linux PTYs clear PARENB; strict readback must reject.
    CHECK(unsupported.status().error.value() == ENOTSUP || unsupported.status().error.value() == EINVAL);
    // Deliberate unsupported value in a fixed-underlying-type enum.
    // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
    profile.baud_rate = static_cast<uart::BaudRate>(12345);
    CHECK(!uart::SerialPort::open(pty.path.data(), profile).ok());
    CHECK(!uart::SerialPort::open("/dev/null", {}).ok());
    CHECK(!uart::SerialPort::open("/missing/sbus", {}).ok());
    uart::SerialPort closed;
    CHECK(!closed.configuration().ok());
    CHECK(!closed.discard_input().ok());
    CHECK(!closed.pending_bytes().ok());
}

/** Verify byte-at-a-time frames, literal ff unescaping and ordered flag events. */
void test_frames() {
    Pty pty;
    Reader reader;
    CHECK(reader.open(pty.path.data(), pty_config()).ok());
    std::array<std::uint8_t, 25> frame{};
    frame[0] = 0x0f;
    for (std::size_t i = 1; i <= 22; ++i)
        frame[i] = 0xff;
    std::uint64_t session = 0;
    for (std::size_t i = 0; i < frame.size(); ++i) {
        pty.send(std::span{frame}.subspan(i, 1));
        const auto batch = receive(reader);
        CHECK(batch.discontinuity == Discontinuity::none);
        CHECK(batch.event_count == (i == 24 ? 1U : 0U));
        CHECK(batch.session != 0);
        session = batch.session;
        if (i == 24) {
            for (const auto channel : batch.events[0].frame.channels)
                CHECK(channel == 2047);
            CHECK(batch.captured_at <= std::chrono::steady_clock::now());
        }
    }
    std::array<std::uint8_t, 75> frames{};
    frames[0] = frames[25] = frames[50] = 0x0f;
    frames[23] = 4;
    frames[48] = 8;
    pty.send(frames);
    const auto batch = receive(reader);
    CHECK(batch.event_count == 3 && batch.session == session);
    CHECK(batch.events[0].frame.frame_lost);
    CHECK(batch.events[1].frame.failsafe);
    CHECK(!batch.events[2].frame.frame_lost && !batch.events[2].frame.failsafe);
    const auto started = std::chrono::steady_clock::now();
    CHECK(receive(reader).raw_size == 0);
    CHECK(std::chrono::steady_clock::now() - started >= 10ms);
    frames[24] = 1;
    pty.send(std::span{frames}.first(25));
    CHECK(receive(reader).events[0].kind == protocol::EventKind::rejected);

    pty.send(std::span{frame}.first(10));
    CHECK(receive(reader).event_count == 0);
    pty.send(frame); // Stale kernel bytes and partial parser state must both go away.
    std::this_thread::sleep_for(2ms);
    CHECK(reader.open(pty.path.data(), pty_config()).ok());
    CHECK(receive(reader, 0ms).raw_size == 0);
    pty.send(frame);
    const auto reopened = receive(reader);
    CHECK(reopened.event_count == 1 && reopened.session > session);
}

/** Inject kernel-encoded marker bytes at the read syscall boundary. */
ReadBatch inject(Reader& reader, const Pty& pty, std::span<const std::uint8_t> bytes) {
    const std::array<std::uint8_t, 1> trigger{0};
    injected = bytes;
    pty.send(trigger);
    return receive(reader);
}

/** Verify marker splitting and that an error after a valid frame closes the port. */
void test_markers() {
    Pty pty;
    Reader reader;
    CHECK(reader.open(pty.path.data(), pty_config()).ok());
    const std::array<std::uint8_t, 2> start{0x0f, 0xff};
    CHECK(inject(reader, pty, start).event_count == 0);
    std::array<std::uint8_t, 24> tail{};
    tail[0] = 0xff;
    const auto escaped = inject(reader, pty, tail);
    CHECK(escaped.event_count == 1 && escaped.events[0].frame.channels[0] == 255);
    std::array<std::uint8_t, 27> error{};
    error[0] = 0x0f;
    error[25] = 0xff;
    error[26] = 0;
    const std::array<std::uint8_t, 1> trigger{0};
    injected = error;
    pty.send(trigger);
    auto result = reader.read(20ms);
    CHECK(!result.ok() && result.status().error.value() == EILSEQ);
    CHECK(!reader.configuration().ok());
    CHECK(!reader.read(0ms).ok());
    CHECK(reader.open(pty.path.data(), pty_config()).ok());
    const std::array<std::uint8_t, 1> ff{0xff};
    CHECK(inject(reader, pty, ff).event_count == 0);
    injected = trigger;
    pty.send(trigger);
    result = reader.read(20ms);
    CHECK(!result.ok() && result.status().error.value() == EILSEQ);
}

/** Exercise backlog, caller stalls, old partial frames, cancellation and hangup. */
void test_discontinuities() {
    Pty pty;
    Reader reader;
    auto config = pty_config();
    CHECK(reader.open(pty.path.data(), config).ok());
    const auto session = receive(reader, 0ms).session;
    std::array<std::uint8_t, 512> backlog{};
    backlog.fill(0x0f);
    pty.send(backlog);
    std::this_thread::sleep_for(2ms);
    const auto discarded = receive(reader);
    CHECK(discarded.discontinuity == Discontinuity::backlog);
    CHECK(discarded.event_count == 0 && discarded.session > session);
    CHECK(receive(reader, 0ms).raw_size == 0);
    config.maximum_service_gap = 30ms;
    CHECK(reader.open(pty.path.data(), config).ok());
    std::this_thread::sleep_for(40ms);
    CHECK(receive(reader, 0ms).discontinuity == Discontinuity::service_gap);
    const std::array<std::uint8_t, 1> header{0x0f};
    pty.send(header);
    CHECK(receive(reader).event_count == 0);
    bool expired = false;
    for (int i = 0; i < 10 && !expired; ++i) {
        std::this_thread::sleep_for(5ms);
        const auto batch = receive(reader, 0ms);
        expired = batch.discontinuity == Discontinuity::partial_timeout;
    }
    CHECK(expired);
    std::array<std::uint8_t, 24> old_tail{};
    pty.send(old_tail);
    CHECK(receive(reader).event_count == 0);

    UniqueFd cancellation{::eventfd(1, EFD_NONBLOCK | EFD_CLOEXEC)};
    CHECK(cancellation.get() >= 0);
    pty.send(header); // Cancellation wins over simultaneous readable data.
    auto cancelled = reader.read(20ms, cancellation.get());
    CHECK(!cancelled.ok() && cancelled.status().error.value() == ECANCELED);
    CHECK(!reader.configuration().ok());
    CHECK(reader.open(pty.path.data(), config).ok());
    pty.master.reset();
    CHECK(!reader.read(20ms).ok());
    CHECK(!reader.configuration().ok());
    CHECK(!reader.open("/missing/sbus", config).ok());
    CHECK(!reader.read(0ms).ok());
    config.serial.mark_errors = false;
    CHECK(!reader.open(pty.path.data(), config).ok());
    CHECK(!reader.read(-1ms).ok());
    CHECK(!reader.read(21ms).ok());
}

struct WireValues {
    std::uint16_t button{200};
    std::uint16_t throttle{1000};
    std::uint8_t flags{0};
};
/** Encode a neutral fixture with explicit channels; mapping oracles are in unit tests. */
std::array<std::uint8_t, 25> source_wire(WireValues values = {}) {
    const auto [button, throttle, flags] = values;
    std::array<std::uint16_t, 16> channels{};
    channels.fill(1000);
    channels[5] = button;
    channels[2] = throttle;
    std::array<std::uint8_t, 25> wire{};
    wire[0] = 0x0f;
    wire[23] = flags;
    for (std::size_t channel = 0; channel < channels.size(); ++channel)
        for (std::size_t bit = 0; bit < 11; ++bit)
            if ((channels[channel] & (1U << bit)) != 0)
                wire[1 + (channel * 11 + bit) / 8] |= static_cast<std::uint8_t>(1U << ((channel * 11 + bit) % 8));
    return wire;
}

/** Compose PTY, actual reader/parser, health and snapshot publication without CAN. */
void test_source_pipeline() {
    Pty pty;
    Reader reader;
    CHECK(reader.open(pty.path.data(), pty_config()).ok());
    SourceConfig config;
    config.steering = {200, 1000, 1800, false};
    config.throttle = config.steering;
    config.button_cooldown = 0ms;
    Source source{config};
    // Retain real reader timestamps but inject time for deterministic expiry.
    const auto pump = [&]() {
        const auto result = reader.read(20ms);
        return update_source(source, result, std::chrono::steady_clock::now());
    };
    auto wire = source_wire();
    std::array<std::uint8_t, 25> bad{};
    bad[0] = 0x0f;
    bad[24] = 1;
    pty.send(bad);
    CHECK(pump().last_fault == InputFault::rejected);
    // Split a real frame; partial reception cannot count as health recovery.
    pty.send(std::span{wire}.first(10));
    CHECK(pump().recovery_count == 0);
    pty.send(std::span{wire}.subspan(10));
    CHECK(pump().recovery_count == 1);
    pty.send(wire);
    CHECK(pump().recovery_count == 2);
    pty.send(wire);
    CHECK(pump().health == Health::disabled);
    pty.send(source_wire({.button = 1800}));
    CHECK(pump().sample.valid);
    pty.send(source_wire({.button = 1800, .throttle = 1800}));
    const auto moving = pump();
    CHECK(moving.sample.command.left_rpm == 60 && moving.sample.command.right_rpm == 60);
    std::array<std::uint8_t, 100> merged{};
    for (std::size_t i = 0; i < 4; ++i) {
        const auto part = source_wire({.button = static_cast<std::uint16_t>(i == 3 ? 1800 : 200),
                                       .flags = static_cast<std::uint8_t>(i == 0 ? 12 : 0)});
        std::copy(part.begin(), part.end(), merged.begin() + static_cast<std::ptrdiff_t>(i * 25));
    }
    pty.send(merged);
    const auto failed = pump();
    CHECK(!failed.sample.valid && failed.last_fault == InputFault::failsafe && failed.recovery_count == 0);
    for (int i = 0; i < 3; ++i) {
        pty.send(wire);
        CHECK(!pump().sample.valid);
    }
    pty.send(source_wire({.button = 1800}));
    CHECK(pump().sample.valid);
    const auto before = source.snapshot();
    CHECK(!source.tick(before.sample.captured_at + 100ms).sample.valid);
    CHECK(source.snapshot().sample.captured_at == before.sample.captured_at);
    // A separate producer avoids rolling the injected future clock backwards.
    Source reopened{config};
    CHECK(reader.open(pty.path.data(), pty_config()).ok());
    pty.send(wire);
    const auto result = reader.read(20ms);
    CHECK(update_source(reopened, result, std::chrono::steady_clock::now()).recovery_count == 1);
    pty.master.reset();
    const auto error = reader.read(20ms);
    CHECK(!error.ok());
    CHECK(update_source(reopened, error, std::chrono::steady_clock::now()).fault == InputFault::transport);
    CHECK(!reopened.snapshot().sample.valid);
}
} // namespace

// GNU ld --wrap requires these exact externally visible names.
// NOLINTNEXTLINE(bugprone-reserved-identifier)
extern "C" ssize_t __real_read(int fd, void* buffer, size_t size);
/** Replace one successful tty read with fault-injection bytes, then disarm. */
// NOLINTNEXTLINE(bugprone-reserved-identifier)
extern "C" ssize_t __wrap_read(int fd, void* buffer, size_t size) {
    const auto count = __real_read(fd, buffer, size);
    if (count > 0 && !injected.empty()) {
        CHECK(injected.size() <= size);
        std::memcpy(buffer, injected.data(), injected.size());
        const auto replacement = static_cast<ssize_t>(injected.size());
        injected = {};
        return replacement;
    }
    return count;
}

/** Run deterministic PTY and syscall-fault tests without physical hardware. */
int main() {
    test_uart();
    test_frames();
    test_markers();
    test_discontinuities();
    test_source_pipeline();
    std::puts("SBUS reader PTY tests passed");
}
