#include "application/control/linux_loop.hpp"
#include "platform/linux/can/socket.hpp"

#include <array>
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <linux/can/error.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace {
using namespace std::chrono_literals;
using namespace robot_control;
using namespace application::control;
using namespace communication::canopen;
using platform::linux::can::CanSocket;
using platform::linux::can::ClassicCanFrame;
using Clock = std::chrono::steady_clock;
int failures = 0;
int send_failure = 0;
unsigned rpdo_sends = 0;
/** Preserve the failing assertion and keep checks active under NDEBUG. */
void check(bool condition, const char* expression, int line) {
    if (!condition) {
        ++failures;
        std::cerr << "line=" << line << " " << expression << '\n';
    }
}
#define CHECK(...) check((__VA_ARGS__), #__VA_ARGS__, __LINE__)
/** Terminate a broken fixture before dereferencing an absent resource. */
void require(bool condition) {
    if (!condition) {
        std::cerr << "fixture creation failed\n";
        std::exit(1);
    }
}
/** Explicit virtual bindings and generous software deadlines, not deployment values. */
CycleConfig config() {
    CycleConfig c;
    c.runtime = {drive::PackedHalf::low, drive::PackedHalf::low, 1, 1, 100, 0, 120ms, 80ms, 60ms};
    c.arbiter.max_abs_rpm = 100;
    c.arbiter.sbus_timeout = 80ms;
    c.arbiter.handover_zero_dwell = 30ms;
    return c;
}
/** Confirmed input roles with synthetic neutral endpoints for the PTY fixture. */
input::sbus::SourceConfig source_config() {
    input::sbus::SourceConfig c;
    c.steering = c.throttle = {200, 1000, 1800, false};
    c.timeout = 80ms;
    c.button_cooldown = 30ms;
    return c;
}
/** PTYs explicitly use 8N2; they cannot validate physical parity/inversion. */
input::sbus::ReaderConfig serial_config() {
    input::sbus::ReaderConfig c;
    c.serial.even_parity = false;
    c.maximum_service_gap = 200ms;
    return c;
}
/** Independently serialize protocol bytes; the peer never uses domain codecs. */
ClassicCanFrame frame(std::uint32_t id, std::initializer_list<unsigned> bytes) {
    ClassicCanFrame f{.raw_can_id = id, .payload_length = static_cast<std::uint8_t>(bytes.size())};
    std::size_t i = 0;
    for (auto b : bytes)
        f.data[i++] = static_cast<std::byte>(b);
    return f;
}
/** Recognize zero by wire target bytes, independently of MotionCommand::is_zero. */
bool zero(const ClassicCanFrame& f) {
    return f.data[2] == std::byte{0} && f.data[3] == std::byte{0} && f.data[4] == std::byte{0}
           && f.data[5] == std::byte{0};
}
/** Own a synthetic receiver and a separate CAN peer around the real application. */
struct Fixture {
    platform::linux::UniqueFd master{::posix_openpt(O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC)};
    std::array<char, 256> path{};
    input::sbus::Reader reader;
    input::sbus::Source source{source_config()};
    platform::linux::process::TerminationEvent term;
    CanSocket peer;
    std::unique_ptr<Lifecycle> owner;
    std::unique_ptr<ControlLoop> loop;
    CycleInput input{.coherent = true, .emergency_stop_known = true};
    LoopResult last{};
    std::vector<ClassicCanFrame> trace;
    std::uint16_t status{0x40};
    std::array<unsigned, 4> velocity{};
    bool external_enabled{false};
    int external_rpm{0};
    std::uint64_t external_sequence{0};
    std::uint16_t throttle{1000}, steering{1000}, button{200};
    std::uint8_t flags{0};
    int missing{-1};
    bool uart_on{true};
    unsigned stage{0};
    std::chrono::microseconds maximum_to_wire{};
    /** Create a process-local synchronous termination event before any threads. */
    static platform::linux::process::TerminationEvent termination() {
        auto result = platform::linux::process::TerminationEvent::create();
        require(result.ok());
        return std::move(result).value();
    }
    /** Open only the managed namespace's virtual bus. */
    static CanSocket socket() {
        auto result = CanSocket::open("vcan0");
        require(result.ok());
        return std::move(result).value();
    }
    /** Compose already-open resources without physical device access. */
    Fixture() : term{termination()}, peer{socket()} {
        require(master.get() >= 0 && ::grantpt(master.get()) == 0 && ::unlockpt(master.get()) == 0);
        require(::ptsname_r(master.get(), path.data(), path.size()) == 0);
        require(reader.open(path.data(), serial_config()).ok());
        StackConfig stack{.interface_name = "vcan0",
                          .controller_node_id = 127,
                          .remote_node_id = 1,
                          .heartbeat_timeout = 120ms,
                          .sdo_timeout = 100ms,
                          .tpdo_timeout = 80ms,
                          .tpdo_expected_dlc = {8, 5, 0, 0}};
        auto created = Lifecycle::create(stack, term);
        require(created.ok());
        owner = std::move(created).value();
        feedback();
        require(owner->run_until(Clock::now() + 5ms).ok());
        create_loop();
    }
    /** Always perform the application's explicit bounded cleanup in the fixture. */
    ~Fixture() {
        if (loop)
            static_cast<void>(loop->stop());
    }
    /** Supply only virtual layout readbacks, bound to this owner's current generation. */
    RuntimeLayoutProof proof() const {
        return {.generation = owner->observation_snapshot(Clock::now()).generation,
                .verified_at = Clock::now(),
                .rpdo_cob_id = 0x201,
                .status_cob_id = 0x181,
                .diagnostics_cob_id = 0x281,
                .rpdo_type = 255,
                .status_type = 255,
                .diagnostics_type = 255,
                .rpdo_map = {0x60400010, 0x60ff0320},
                .status_map = {0x60410020, 0x606c0320},
                .diagnostics_map = {0x60610008, 0x603f0020},
                .rpdo_count = 2,
                .status_count = 2,
                .diagnostics_count = 2,
                .command_application = 1};
    }
    /** Attach exactly one application writer. */
    void create_loop(time::Duration timeout = 100ms) {
        auto created = ControlLoop::create(*owner, reader, source, config(), proof(), timeout);
        require(created.ok());
        loop = std::move(created).value();
    }
    /** Publish independent wire status, heartbeat and mode/error feedback. */
    void feedback() {
        if (missing != 0)
            CHECK(peer.send(frame(0x701, {5}), 10ms).ok());
        if (missing != 1)
            CHECK(peer.send(frame(0x181, {status & 255U, static_cast<unsigned>(status) >> 8U, status & 255U,
                                          static_cast<unsigned>(status) >> 8U, velocity[0], velocity[1], velocity[2],
                                          velocity[3]}),
                            10ms)
                      .ok());
        if (missing != 2)
            CHECK(peer.send(frame(0x281, {3, 0, 0, 0, 0}), 10ms).ok());
    }
    /** Encode a real SBUS frame from independent 11-bit channel values. */
    std::array<std::uint8_t, 25> sbus() const {
        std::array<std::uint16_t, 16> channels{};
        channels.fill(1000);
        channels[0] = steering;
        channels[2] = throttle;
        channels[5] = button;
        channels[6] = 1800;
        std::array<std::uint8_t, 25> bytes{};
        bytes[0] = 0x0f;
        bytes[23] = flags;
        for (std::size_t ch = 0; ch < 16; ++ch)
            for (unsigned bit = 0; bit < 11; ++bit)
                if ((channels[ch] & (1U << bit)) != 0) {
                    const auto position = ch * 11 + bit;
                    bytes[1 + position / 8] |= static_cast<std::uint8_t>(1U << (position % 8));
                }
        return bytes;
    }
    /** Drain all actual CAN writes; validate both protocol and zero/enable order. */
    void drain() {
        for (unsigned n = 0; n < 128; ++n) {
            auto received = peer.receive(0ms);
            CHECK(received.ok());
            if (!received.ok())
                return;
            const auto& observation = received.value();
            if (!observation.has_value())
                return;
            const auto f = observation->frame;
            CHECK(f.raw_can_id == 0x201 && f.payload_length == 6);
            trace.push_back(f);
            for (std::size_t axis = 0; axis < 2; ++axis) {
                int rpm =
                    std::to_integer<int>(f.data[2 + axis * 2]) + (std::to_integer<int>(f.data[3 + axis * 2]) << 8);
                if (rpm >= 32768)
                    rpm -= 65536;
                const auto speed = static_cast<std::uint16_t>(rpm * 10);
                velocity[axis * 2] = speed & 255U;
                velocity[axis * 2 + 1] = static_cast<unsigned>(speed) >> 8U;
            }
            const auto word = std::to_integer<unsigned>(f.data[0]);
            CHECK(f.data[1] == std::byte{0});
            if (word == 6) {
                CHECK(zero(f));
                stage = 1;
                status = 0x21;
            } else if (word == 7) {
                CHECK(zero(f) && stage >= 1);
                stage = 2;
                status = 0x23;
            } else if (word == 15) {
                CHECK(stage >= 2);
                stage = 3;
                status = 0x27;
            } else if (word == 2 || word == 0) {
                CHECK(zero(f));
                stage = 0;
                status = word == 2 ? 0x07 : 0x40;
            } else
                CHECK(false);
            if (!zero(f))
                CHECK(word == 15 && stage == 3);
        }
        CHECK(false); // No unbounded drain can conceal a transmit flood.
    }
    /** Advance one real application cycle with independent source/peer stimuli. */
    LoopResult tick() {
        feedback();
        if (uart_on) {
            const auto bytes = sbus();
            CHECK(::write(master.get(), bytes.data(), bytes.size()) == static_cast<ssize_t>(bytes.size()));
        }
        const auto start = Clock::now();
        if (external_enabled) {
            input.external = {.source = command::Source::external,
                              .command = {external_rpm, external_rpm, false},
                              .captured_at = Clock::now(),
                              .session_generation = 1,
                              .authorization_generation = 1,
                              .sequence = ++external_sequence,
                              .valid = true,
                              .coherent = true,
                              .enabled = true};
        }
        last = loop->step(input);
        drain();
        maximum_to_wire =
            std::max(maximum_to_wire, std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - start));
        if (!last.status.ok())
            std::cerr << "loop=" << last.status.operation << " errno=" << last.status.error.value() << '\n';
        return last;
    }
    /** Recover only through neutral, button release and a fresh explicit enable edge. */
    void arm() {
        throttle = steering = 1000;
        flags = 0;
        uart_on = true;
        button = 200;
        for (int i = 0; i < 5; ++i)
            CHECK(!tick().finished);
        if (source.snapshot().sample.enabled) {
            button = 1800;
            CHECK(!tick().finished);
            button = 200;
            for (int i = 0; i < 5; ++i)
                CHECK(!tick().finished);
        }
        button = 1800;
        CHECK(!tick().finished);
        button = 200;
        for (int i = 0; i < 30 && (stage != 3 || !loop->runtime_state().armed); ++i)
            CHECK(!tick().finished);
        CHECK(stage == 3 && loop->runtime_state().armed && source.snapshot().sample.enabled);
        throttle = 1400;
        steering = 1200;
        CHECK(!tick().finished);
        CHECK(!trace.empty() && trace.back().data[2] == std::byte{75} && trace.back().data[4] == std::byte{25});
    }
    /** Check every newly emitted target remains zero. */
    void zeros_since(std::size_t begin) const {
        for (auto i = begin; i < trace.size(); ++i)
            CHECK(zero(trace[i]));
    }
};
/** Check closed-loop continuity, source loss, emergency stop and fresh rearming. */
void control_cases() {
    Fixture f;
    f.arm();
    for (int i = 0; i < 100; ++i) {
        CHECK(!f.tick().finished);
        CHECK(f.trace.back().data[2] == std::byte{75} && f.trace.back().data[4] == std::byte{25});
    }
    CHECK(f.maximum_to_wire < 100ms);
    std::cout << "closed_loop_cycles=" << f.last.cycles << " max_tick_to_peer_us=" << f.maximum_to_wire.count()
              << " max_lateness_us="
              << std::chrono::duration_cast<std::chrono::microseconds>(f.last.maximum_lateness).count()
              << " max_cycle_us="
              << std::chrono::duration_cast<std::chrono::microseconds>(f.last.maximum_cycle_time).count()
              << " missed_periods=" << f.last.missed_periods << '\n';
    auto before = f.trace.size();
    f.flags = 12;
    CHECK(!f.tick().finished);
    f.zeros_since(before);
    f.flags = 0;
    for (int i = 0; i < 5; ++i)
        CHECK(!f.tick().finished);
    f.zeros_since(before);
    f.arm();
    f.input.emergency_stop_active = true;
    before = f.trace.size();
    CHECK(!f.tick().finished);
    f.input.emergency_stop_active = false;
    for (int i = 0; i < 4; ++i)
        CHECK(!f.tick().finished);
    f.zeros_since(before);
    f.arm();
    f.uart_on = false;
    for (int i = 0; i < 12; ++i)
        CHECK(!f.tick().finished);
    CHECK(zero(f.trace.back()) && !f.loop->runtime_state().armed);
    before = f.trace.size();
    f.uart_on = true;
    for (int i = 0; i < 4; ++i)
        CHECK(!f.tick().finished);
    f.zeros_since(before);
    f.arm();
    // Fault followed by healthy data within the same owner run must still revoke.
    before = f.trace.size();
    CHECK(f.peer.send(frame(0x281, {3, 0, 0, 1, 0}), 10ms).ok());
    CHECK(!f.tick().finished);
    f.zeros_since(before);
    for (int i = 0; i < 4; ++i)
        CHECK(!f.tick().finished);
    f.zeros_since(before);
    f.arm();
    for (int missing = 0; missing < 3; ++missing) {
        f.missing = missing;
        for (int i = 0; i < 25 && f.loop->runtime_state().armed; ++i)
            CHECK(!f.tick().finished);
        CHECK(!f.loop->runtime_state().armed && zero(f.trace.back()));
        before = f.trace.size();
        f.missing = -1;
        for (int i = 0; i < 4; ++i)
            CHECK(!f.tick().finished);
        f.zeros_since(before);
        f.arm();
    }
    // A skipped owner interval must not turn a previously issued decision into a fresh one.
    std::this_thread::sleep_for(70ms);
    before = f.trace.size();
    CHECK(!f.tick().finished);
    f.zeros_since(before);
    f.arm();
    CHECK(::kill(::getpid(), SIGTERM) == 0);
    const auto start = Clock::now();
    before = f.trace.size();
    const auto stopped = f.tick();
    CHECK(stopped.finished && stopped.status.ok() && stopped.exit == LifecycleExit::sigterm);
    CHECK(!stopped.source.sample.valid && stopped.source.sample.command.is_zero());
    CHECK(Clock::now() - start < 100ms && stopped.shutdown_elapsed < 100ms);
    f.zeros_since(before);
    const auto count = rpdo_sends;
    CHECK(f.loop->stop().status.ok());
    CHECK(rpdo_sends == count);
}
/** Validate source handover and replay of an envelope generated by the real cycle. */
void handover_and_replay() {
    Fixture f;
    f.arm();
    f.throttle = f.steering = 1000;
    f.external_enabled = true;
    for (int i = 0; i < 25; ++i) {
        CHECK(!f.tick().finished);
        CHECK(zero(f.trace.back()));
    }
    CHECK(f.last.control.selected.source == command::Source::external && f.loop->runtime_state().armed);
    f.external_rpm = -20;
    for (int i = 0; i < 4; ++i) {
        CHECK(!f.tick().finished);
        CHECK(f.trace.back().data[2] == std::byte{0xec} && f.trace.back().data[3] == std::byte{0xff});
        CHECK(f.trace.back().data[4] == std::byte{0xec} && f.trace.back().data[5] == std::byte{0xff});
    }
    const auto old_request = f.last.control.request;
    const auto old_session = f.last.runtime_session;
    auto before = f.trace.size();
    f.throttle = 1400;
    CHECK(!f.tick().finished);
    f.zeros_since(before); // Previously consumed SBUS authorization cannot retake motion.
    CHECK(f.loop->stop().status.ok());
    f.drain();
    f.loop.reset();
    f.feedback();
    CHECK(f.owner->run_until(Clock::now() + 5ms).ok());
    auto created = RuntimeSession::create(*f.owner, config().runtime, f.proof());
    require(created.ok());
    auto runtime = std::move(created).value();
    CHECK(runtime->session() != old_session);
    before = f.trace.size();
    CHECK(!runtime->submit({old_request, old_session}).ok());
    f.drain();
    f.zeros_since(before);
    auto expired = old_request;
    expired.feedback_epoch = runtime->state().epoch;
    expired.issued_at = Clock::now() - 1s;
    const auto rejected = runtime->submit({expired, runtime->session()});
    CHECK(rejected.ok() && !rejected.value().accepted);
    f.drain();
    f.zeros_since(before);
    CHECK(runtime->stop().ok());
    f.drain();
}

/** Toggle only the script-owned virtual interface in its private network namespace. */
void link(CanSocket& socket, bool up) {
    ifreq request{};
    std::strcpy(request.ifr_name, "vcan0");
    require(::ioctl(socket.fd(), SIOCGIFFLAGS, &request) == 0);
    request.ifr_flags = static_cast<short>(up ? request.ifr_flags | IFF_UP : request.ifr_flags & ~IFF_UP);
    require(::ioctl(socket.fd(), SIOCSIFFLAGS, &request) == 0);
}
/** Verify terminal link/layout errors, explicit restart, send failures and UART disconnect. */
void lifecycle_cases() {
    {
        Fixture f;
        f.arm();
        const auto before = f.trace.size();
        CHECK(f.peer.send(frame(0x701, {0}), 10ms).ok());
        CHECK(f.tick().finished);
        CHECK(!f.last.status.ok() && !f.last.stop_status.ok());
        f.zeros_since(before);
        CHECK(f.trace.size() == before); // Unknown layout forbids even zero writes.
        CHECK(f.loop->runtime_state().stopped);
        f.loop.reset();
        CHECK(f.owner->reopen().ok());
        CHECK(f.reader.open(f.path.data(), serial_config()).ok());
        f.feedback();
        CHECK(f.owner->run_until(Clock::now() + 5ms).ok());
        f.create_loop();
        for (int i = 0; i < 5; ++i)
            CHECK(!f.tick().finished);
        f.zeros_since(before);
        f.arm();
        CHECK(f.loop->stop().status.ok());
        f.drain();
    }
    {
        Fixture f;
        f.arm();
        link(f.peer, false);
        const auto before = f.trace.size();
        // No peer sends on a down link: step alone must observe administrative loss.
        auto result = f.loop->step(f.input);
        if (!result.finished)
            result = f.loop->step(f.input);
        CHECK(result.finished && !result.status.ok() && f.loop->runtime_state().stopped);
        const auto down = f.peer.receive(0ms);
        CHECK(!down.ok() && down.status().error.value() == EIO);
        int socket_error = 0;
        socklen_t length = sizeof(socket_error);
        CHECK(::getsockopt(f.peer.fd(), SOL_SOCKET, SO_ERROR, &socket_error, &length) == 0);
        CHECK(socket_error == ENETDOWN);
        link(f.peer, true);
        f.drain();
        f.zeros_since(before);
        CHECK(f.loop->step(f.input).finished); // Link recovery cannot restart this loop.
    }
    {
        Fixture f;
        f.arm();
        const auto before = f.trace.size();
        can_frame error{};
        error.can_id = CAN_ERR_FLAG | CAN_ERR_BUSOFF;
        error.can_dlc = CAN_ERR_DLC;
        CHECK(::write(f.peer.fd(), &error, CAN_MTU) == CAN_MTU);
        CHECK(f.tick().finished && f.loop->runtime_state().stopped);
        f.zeros_since(before);
    }
    {
        Fixture f;
        f.arm();
        const auto count = rpdo_sends;
        send_failure = EIO;
        const auto stopped = f.loop->stop();
        CHECK(stopped.finished && !stopped.status.ok() && !stopped.stop_status.ok());
        CHECK(f.loop->stop().status.error == stopped.status.error && rpdo_sends == count + 1);
    }
    {
        Fixture f;
        CHECK(f.loop->stop().status.ok());
        f.drain();
        f.loop.reset();
        CHECK(f.reader.open(f.path.data(), serial_config()).ok());
        f.create_loop(1ns); // Deliberately impossible reporting budget, not a motion limit.
        f.arm();
        const auto before = f.trace.size();
        const auto stopped = f.loop->stop();
        f.drain();
        f.zeros_since(before);
        CHECK(stopped.finished && stopped.stop_status.ok() && stopped.status.error.value() == ETIMEDOUT);
        CHECK(f.loop->stop().status.error == stopped.status.error);
    }
    for (int failure : {EIO, -1}) {
        Fixture f;
        f.arm();
        const auto count = rpdo_sends;
        send_failure = failure;
        const auto result = f.tick();
        CHECK(result.finished && !result.status.ok() && !result.stop_status.ok());
        CHECK(result.status.operation == "runtime_rpdo_send" && result.status.error.value() == EIO);
        CHECK(rpdo_sends == count + 1 && f.loop->runtime_state().stopped);
        CHECK(!f.loop->stop().status.ok() && rpdo_sends == count + 1);
    }
    {
        Fixture f;
        f.arm();
        f.master.reset();
        const auto before = f.trace.size();
        const auto result = f.loop->step(f.input);
        f.drain();
        CHECK(result.finished && !result.status.ok() && result.stop_status.ok());
        f.zeros_since(before);
    }
}
/** A queued termination must win even when the scheduled cycle is already late. */
void late_signal() {
    Fixture f;
    f.arm();
    std::this_thread::sleep_for(12ms);
    const auto before = f.trace.size();
    CHECK(::kill(::getpid(), SIGTERM) == 0);
    const auto result = f.tick();
    CHECK(result.finished && result.exit == LifecycleExit::sigterm);
    f.zeros_since(before);
    if (!result.finished)
        static_cast<void>(f.tick());
}
} // namespace
// NOLINTNEXTLINE(bugprone-reserved-identifier): GNU ld --wrap ABI.
extern "C" ssize_t __real_send(int, const void*, std::size_t, int);
/** Inject failure only at the final RPDO syscall, never into independent peer feedback. */
// NOLINTNEXTLINE(bugprone-reserved-identifier): GNU ld --wrap ABI.
extern "C" ssize_t __wrap_send(int fd, const void* data, std::size_t size, int flags) {
    const auto* f = static_cast<const can_frame*>(data);
    if (size == CAN_MTU && f->can_id == 0x201) {
        ++rpdo_sends;
        if (send_failure) {
            const auto fail = send_failure;
            send_failure = 0;
            errno = fail == -1 ? 0 : fail;
            return fail == -1 ? static_cast<ssize_t>(CAN_MTU - 1) : -1;
        }
    }
    return __real_send(fd, data, size, flags);
}
/** Execute solely under the managed namespace runner, never on a physical bus. */
int main() {
    const auto* name = std::getenv("ROBOT_CONTROL_TEST_VCAN_INTERFACE");
    const auto* toggle = std::getenv("ROBOT_CONTROL_TEST_ALLOW_VCAN_LINK_TOGGLE");
    if (!name || std::string{name} != "vcan0" || !toggle || std::string{toggle} != "1")
        return 77;
    control_cases();
    handover_and_replay();
    lifecycle_cases();
    late_signal();
    return failures == 0 ? 0 : 1;
}
