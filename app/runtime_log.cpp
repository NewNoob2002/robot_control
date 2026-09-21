#include "app/runtime_log.hpp"
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <poll.h>
#include <sstream>
#include <stdexcept>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include "domain/drive/cia402.hpp"
#include "service/logging/logger.hpp"

namespace robot_control::app {
namespace {
using Fd = platform::linux::UniqueFd;
using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;

/** Report startup/collector syscall errors with their operation and errno. */
void require(bool ok, const char* operation) {
    if (!ok)
        throw std::runtime_error(std::string{operation} + ": " + std::strerror(errno));
}
/** Create a nonblocking close-on-exec pipe; ownership transfers into the returned pair. */
std::array<Fd, 2> pipe_pair() {
    int descriptors[2]{};
    require(::pipe2(descriptors, O_NONBLOCK | O_CLOEXEC) == 0, "pipe2");
    return {Fd{descriptors[0]}, Fd{descriptors[1]}};
}
/** Mirror an already formatted record without blocking on a slow/closed terminal; failure is counted by caller. */
bool mirror_terminal(const char* data, std::size_t size) noexcept {
    auto count = ::send(STDOUT_FILENO, data, size, MSG_DONTWAIT | MSG_NOSIGNAL);
    if (count >= 0)
        return static_cast<std::size_t>(count) == size;
    if (errno != ENOTSOCK)
        return false;
    // Reopen instead of changing the shared terminal/pipe open-file flags.
    Fd output{::open("/proc/self/fd/1", O_WRONLY | O_NONBLOCK | O_CLOEXEC | O_APPEND)};
    if (!output)
        return false;
    count = ::write(output.get(), data, size);
    return count >= 0 && static_cast<std::size_t>(count) == size;
}
/** Child-only rotating file; segments never exceed the configured bound and raw packets stay intact. */
class RotatingFile final {
  public:
    /** Own a new file inside a private run directory; only the collector may access this object. */
    RotatingFile(std::string path, std::size_t limit, unsigned count)
        : path_{std::move(path)}, limit_{limit}, count_{count} {
        open();
    }
    /** Ensure the next complete record fits, rotating only this run's files when needed. */
    void reserve(std::size_t bytes) {
        if (bytes > limit_)
            throw std::runtime_error("log record exceeds file bound");
        if (used_ + bytes > limit_) {
            require(fd_.close().ok(), "close rotated log");
            for (unsigned i = count_ - 1; i > 0; --i) {
                const std::string from = i == 1 ? path_ : path_ + "." + std::to_string(i - 1);
                const std::string to = path_ + "." + std::to_string(i);
                if (::rename(from.c_str(), to.c_str()) != 0 && errno != ENOENT)
                    require(false, "rotate log");
            }
            if (count_ == 1)
                require(::unlink(path_.c_str()) == 0, "rotate sole log");
            open();
            used_ = 0;
        }
    }
    /** Write a complete record on the collector only; parent health/pipe bounds cover disk stalls. */
    void write(const void* buffer, std::size_t size) {
        reserve(size);
        const auto* bytes = static_cast<const char*>(buffer);
        std::size_t offset = 0;
        while (offset < size) {
            const auto count = ::write(fd_.get(), bytes + offset, size - offset);
            if (count < 0 && errno == EINTR)
                continue;
            require(count > 0, "write log");
            offset += static_cast<std::size_t>(count);
        }
        used_ += size;
    }
    /** Prepare bounded EasyLogger output and redirect only the child stderr to the current segment. */
    void logger_sink() {
        reserve(512);
        require(::dup2(fd_.get(), STDERR_FILENO) >= 0, "dup2 diagnostics log");
    }
    /** Account for the bytes actually written by EasyLogger through its append descriptor. */
    bool update_size() {
        const auto size = ::lseek(fd_.get(), 0, SEEK_END);
        require(size >= 0, "measure diagnostic log");
        const auto previous = used_;
        used_ = static_cast<std::size_t>(size);
        if (used_ > limit_ || used_ < previous || used_ - previous > 512)
            throw std::runtime_error("EasyLogger record exceeds bound");
        std::array<char, 512> formatted{};
        const auto bytes = used_ - previous;
        require(::pread(fd_.get(), formatted.data(), bytes, static_cast<off_t>(previous))
                    == static_cast<ssize_t>(bytes),
                "read formatted log");
        return mirror_terminal(formatted.data(), bytes);
    }

  private:
    /** Open a new segment without following links or overwriting an existing file. */
    void open() {
        fd_.reset(::open(path_.c_str(), O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600));
        require(static_cast<bool>(fd_), "create log segment");
    }
    std::string path_;
    std::size_t limit_, used_{0};
    unsigned count_;
    Fd fd_;
};

/** Drain app text and fixed records to bounded files, emitting human telemetry at1Hz and on state changes. */
int collect(const application::config::Startup& config, const std::string& directory, int raw, int text,
            int heartbeat) {
    RotatingFile application_file{directory + "/application.log", config.log_file_bytes, config.log_files};
    RotatingFile trace{directory + "/trace.bin", config.log_file_bytes, config.log_files};
    RotatingFile diagnostic{directory + "/diagnostics.log", config.log_file_bytes, config.log_files};
    std::ofstream effective{directory + "/config.conf"};
    application::config::dump(effective, config);
    effective.close();
    if (!effective)
        return 1;
    diagnostic.logger_sink();
    service::logging::Logger logger{true};
    static_cast<void>(diagnostic.update_size());
    std::uint64_t console_dropped = 0;
    const auto output = [&](RotatingFile& file, const char* tag, const char* event, const std::string& value) {
        file.logger_sink();
        logger.log(service::logging::Severity::info, tag, event, value);
        if (logger.health().output_failures)
            throw std::runtime_error("EasyLogger file output failed");
        if (!file.update_size())
            ++console_dropped;
    };
    const auto emit = [&](const char* event, const std::string& value) {
        output(diagnostic, "vehicle", event, value);
    };
    std::string pending;
    pending.reserve(240);
    emit("logger_start", "pid=" + std::to_string(::getpid()) + " record_version=1");
    bool raw_open = true, text_open = true, ended = false;
    std::int64_t sequence = 0, last_summary = 0;
    std::array<std::int64_t, 4> previous{-1, -1, -1, -1};
    auto next_heartbeat = Clock::time_point{};
    while (raw_open || text_open) {
        const auto now = Clock::now();
        if (now >= next_heartbeat) {
            const auto stamp = std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count();
            if (::write(heartbeat, &stamp, sizeof(stamp)) != static_cast<ssize_t>(sizeof(stamp)))
                return 1;
            next_heartbeat = now + 100ms;
        }
        pollfd descriptors[2]{{raw_open ? raw : -1, POLLIN, 0}, {text_open ? text : -1, POLLIN, 0}};
        const int ready = ::poll(descriptors, 2, 50);
        if (ready < 0 && errno == EINTR)
            continue;
        require(ready >= 0, "poll log pipes");
        if (descriptors[1].revents & (POLLIN | POLLHUP)) {
            std::array<char, 4096> buffer{};
            const auto size = ::read(text, buffer.data(), buffer.size());
            if (size == 0) {
                text_open = false;
                if (!pending.empty()) {
                    output(application_file, "app", "stream_tail", pending);
                    pending.clear();
                }
            } else if (size > 0) {
                for (ssize_t i = 0; i < size; ++i) {
                    const char c = buffer[static_cast<std::size_t>(i)];
                    if (c == '\r')
                        continue;
                    if (c == '\n') {
                        output(application_file, "app", "stream", pending);
                        pending.clear();
                    } else {
                        pending += c >= ' ' && c <= '~' ? c : '?';
                        if (pending.size() == 240) {
                            output(application_file, "app", "stream_fragment", pending);
                            pending.clear();
                        }
                    }
                }
            } else if (errno != EAGAIN && errno != EINTR)
                require(false, "read app output");
        }
        if (descriptors[0].revents & (POLLIN | POLLHUP)) {
            // One complete atomic packet at a time keeps the collector bounded and avoids partial framing state.
            Packet packet{};
            const auto size = ::read(raw, packet.data(), sizeof(packet));
            if (size == 0) {
                raw_open = false;
                continue;
            }
            if (size < 0 && (errno == EAGAIN || errno == EINTR))
                continue;
            if (size != static_cast<ssize_t>(sizeof(packet)) || ended || packet[0] != record_magic
                || packet[1] != sequence++ || packet[2] < 0 || packet[2] > static_cast<std::int64_t>(RecordKind::end)
                || packet[3] < 0)
                return 1;
            trace.write(packet.data(), sizeof(packet));
            const auto kind = static_cast<RecordKind>(packet[2]);
            if (kind == RecordKind::cycle) {
                const bool servo_enabled = domain::drive::velocity_motion_eligible(
                    domain::drive::decode_dual_axis_status(static_cast<std::uint32_t>(packet[14])),
                    static_cast<std::int8_t>(packet[16]));
                const std::array state{packet[8], packet[9], packet[19], static_cast<std::int64_t>(servo_enabled)};
                if (state != previous || packet[3] - last_summary >= 1000000000) {
                    std::ostringstream line;
                    line << "mono_ns=" << packet[3] << " cycles=" << packet[4] << " seq=" << packet[5]
                         << " auth=" << packet[6] << " enabled=" << packet[8] << " input_fault=" << packet[9]
                         << " target_l=" << packet[10] << " target_r=" << packet[11]
                         << " feedback_l_tenths=" << packet[12] << " feedback_r_tenths=" << packet[13]
                         << " status_raw=" << packet[14] << " fault_raw=" << packet[15] << " mode=" << packet[16]
                         << " armed=" << packet[19] << " servo_enabled=" << servo_enabled
                         << " zero_hold=" << (servo_enabled && packet[10] == 0 && packet[11] == 0);
                    emit("control_state", line.str());
                    std::ostringstream timing;
                    timing << "mono_ns=" << packet[3] << " input_age_us=" << packet[20] << " hb_age_us=" << packet[21]
                           << " tpdo_age_us=" << packet[22] << " missed=" << packet[23] << " max_late_us=" << packet[24]
                           << " max_cycle_us=" << packet[25] << " transient_samples=" << packet[26]
                           << " monitor_fault=" << packet[27];
                    emit("control_timing", timing.str());
                    previous = state;
                    last_summary = packet[3];
                }
            } else if (kind == RecordKind::stop) {
                emit("control_stop", "mono_ns=" + std::to_string(packet[3]) + " reason=" + std::to_string(packet[4])
                                         + " primary_errno=" + std::to_string(packet[5])
                                         + " cleanup_errno=" + std::to_string(packet[6]));
            } else if (kind == RecordKind::end) {
                ended = true;
                emit("logger_end", "records=" + std::to_string(sequence) + " app_result=" + std::to_string(packet[4]));
            }
        }
    }
    emit("console_summary", "dropped_records=" + std::to_string(console_dropped));
    return ended ? 0 : 1;
}
} // namespace

LogSession::LogSession(const application::config::Startup& config) {
    try {
        std::filesystem::create_directories(config.log_dir);
        std::string path = config.log_dir + "/run-" + std::to_string(::getpid()) + "-XXXXXX";
        require(::mkdtemp(path.data()) != nullptr, "create run directory");
        directory_ = std::filesystem::absolute(path).string();
        auto raw = pipe_pair();
        auto text = pipe_pair();
        auto heartbeat = pipe_pair();
        std::cout << "log_dir=" << directory_ << '\n' << std::flush;
        require(std::cout.good(), "announce run directory");
        saved_out_.reset(::dup(STDOUT_FILENO));
        saved_err_.reset(::dup(STDERR_FILENO));
        require(saved_out_ && saved_err_, "save terminal descriptors");
        const auto parent = ::getpid();
        child_ = ::fork();
        require(child_ >= 0, "fork logger");
        if (child_ == 0) {
            raw[1].reset();
            text[1].reset();
            heartbeat[0].reset();
            saved_out_.reset();
            saved_err_.reset();
            int code = 1;
            try {
                require(::prctl(PR_SET_PDEATHSIG, SIGKILL) == 0 && ::getppid() == parent, "logger parent identity");
                require(::signal(SIGINT, SIG_IGN) != SIG_ERR && ::signal(SIGTERM, SIG_IGN) != SIG_ERR,
                        "logger signals");
                require(::signal(SIGPIPE, SIG_IGN) != SIG_ERR, "logger SIGPIPE");
                code = collect(config, directory_, raw[0].get(), text[0].get(), heartbeat[1].get());
            } catch (const std::exception& error) {
                std::ofstream failure{directory_ + "/logger-error.log"};
                failure << error.what() << '\n';
                code = 1;
            }
            ::_exit(code);
        }
        raw[0].reset();
        text[0].reset();
        heartbeat[1].reset();
        raw_ = std::move(raw[1]);
        heartbeat_ = std::move(heartbeat[0]);
        writer_ = RecordWriter{raw_.get()};
        pollfd ready{heartbeat_.get(), POLLIN, 0};
        const int readiness = ::poll(&ready, 1, 3000);
        require(readiness >= 0, "poll logger readiness");
        if (readiness == 0 || !(ready.revents & POLLIN))
            throw std::runtime_error("logger not ready; inspect " + directory_ + "/logger-error.log");
        require(healthy(), "logger initial health");
        require(::dup2(text[1].get(), STDOUT_FILENO) >= 0, "redirect stdout");
        redirected_ = true;
        require(::dup2(text[1].get(), STDERR_FILENO) >= 0, "redirect stderr");
        std::cout << "event=app_start pid=" << parent << " logger_pid=" << child_ << '\n';
        application::config::dump(std::cout, config);
        std::cout.flush();
    } catch (...) {
        static_cast<void>(finish(1));
        throw;
    }
}
LogSession::~LogSession() {
    static_cast<void>(finish(1));
}
bool LogSession::healthy() noexcept {
    if (finished_ || child_ <= 0 || !writer_.good() || !std::cout.good() || !std::cerr.good())
        return false;
    std::array<std::int64_t, 256> stamps{};
    const auto size = ::read(heartbeat_.get(), stamps.data(), sizeof(stamps));
    if (size > 0 && size % static_cast<ssize_t>(sizeof(stamps[0])) == 0) {
        last_heartbeat_ =
            Clock::time_point{std::chrono::nanoseconds{stamps[static_cast<std::size_t>(size) / sizeof(stamps[0]) - 1]}};
    } else if (size == 0 || (size < 0 && errno != EAGAIN && errno != EINTR))
        return false;
    int status = 0;
    if (::waitpid(child_, &status, WNOHANG) != 0) {
        child_ = -1;
        return false;
    }
    const auto now = Clock::now();
    return now >= last_heartbeat_ && now - last_heartbeat_ < 500ms;
}
bool LogSession::finish(int result) noexcept {
    if (finished_)
        return result_;
    finished_ = true;
    writer_.append(RecordKind::end, {result});
    bool streams = true;
    if (redirected_) {
        std::cout.flush();
        std::cerr.flush();
        streams = std::cout.good() && std::cerr.good();
        streams = (::dup2(saved_out_.get(), STDOUT_FILENO) >= 0) && streams;
        streams = (::dup2(saved_err_.get(), STDERR_FILENO) >= 0) && streams;
        std::cout.clear();
        std::cerr.clear();
        redirected_ = false;
    }
    raw_.reset(); // EOF after text redirection; keep heartbeat reader alive while child drains.
    const auto deadline = Clock::now() + 2s;
    while (child_ > 0 && Clock::now() < deadline) {
        int status = 0;
        const auto waited = ::waitpid(child_, &status, WNOHANG);
        if (waited == child_) {
            result_ = streams && writer_.good() && WIFEXITED(status) && WEXITSTATUS(status) == 0;
            child_ = -1;
            return result_;
        }
        if (waited < 0 && errno != EINTR)
            break;
        static_cast<void>(::poll(nullptr, 0, 10));
    }
    if (child_ > 0) {
        static_cast<void>(::kill(child_, SIGKILL));
        static_cast<void>(::waitpid(child_, nullptr, WNOHANG));
        child_ = -1;
    }
    return false;
}
} // namespace robot_control::app
