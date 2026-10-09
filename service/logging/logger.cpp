#include "service/logging/logger.hpp"
#include "service/logging/stderr_sink.hpp"

#include <spdlog/pattern_formatter.h>
#include <spdlog/spdlog.h>

#include <chrono>
#include <memory>
#include <mutex>
#include <sstream>
#include <sys/syscall.h>
#include <unistd.h>

namespace robot_control::service::logging {
namespace {

/** Convert severity to a stable journal-friendly label. */
std::string_view label(const Severity severity) noexcept {
    switch (severity) {
        case Severity::debug:
            return "DEBUG";
        case Severity::info:
            return "INFO";
        case Severity::warning:
            return "WARNING";
        case Severity::error:
            return "ERROR";
        case Severity::critical:
            return "CRITICAL";
    }
    return "UNKNOWN";
}

/** Map the project severity without exposing spdlog types in the public logger API. */
spdlog::level::level_enum spdlog_level(const Severity severity) noexcept {
    switch (severity) {
        case Severity::debug:
            return spdlog::level::debug;
        case Severity::info:
            return spdlog::level::info;
        case Severity::warning:
            return spdlog::level::warn;
        case Severity::error:
            return spdlog::level::err;
        case Severity::critical:
            return spdlog::level::critical;
    }
    return spdlog::level::err;
}

/** Preserve the established single-letter diagnostic format. */
char level_char(const Severity severity) noexcept {
    switch (severity) {
        case Severity::debug:
            return 'D';
        case Severity::info:
            return 'I';
        case Severity::warning:
            return 'W';
        case Severity::error:
        case Severity::critical:
            return 'E';
    }
    return 'E';
}

/** Share one serialized stderr sink for the process-wide output/health contract; no registry ownership. */
std::shared_ptr<StderrSink>& shared_sink() {
    static auto sink = [] {
        auto value = std::make_shared<StderrSink>();
        value->set_formatter(std::make_unique<spdlog::pattern_formatter>("%v", spdlog::pattern_time_type::local, ""));
        return value;
    }();
    return sink;
}

} // namespace

Logger::Logger(bool detailed) : detailed_{detailed} {
    // Ensure shared sink is initialized
    static_cast<void>(shared_sink());
}

Logger::~Logger() = default;

void Logger::log(const Severity severity, const std::string_view module, const std::string_view event,
                 const std::string_view context) {
    const std::scoped_lock lock{mutex_};
    write_line(severity, module, LineContent{event, context}, 0);
}

void Logger::log_throttled(std::string key, const std::chrono::milliseconds interval, const Severity severity,
                           const std::string_view module, const std::string_view event, const std::string_view context,
                           const std::chrono::steady_clock::time_point now) {
    const std::scoped_lock lock{mutex_};
    auto position = throttles_.find(key);
    if (position == throttles_.end()) {
        if (throttles_.size() >= max_throttle_keys) {
            write_line(severity, module, LineContent{event, context}, 0);
            return;
        }
        position = throttles_.try_emplace(std::move(key)).first;
    }
    auto& state = position->second;
    if (state.initialized && interval > std::chrono::milliseconds::zero() && (now - state.last_emitted) < interval) {
        ++state.suppressed;
        return;
    }
    write_line(severity, module, LineContent{event, context}, state.suppressed);
    state.last_emitted = now;
    state.suppressed = 0;
    state.initialized = true;
}

LoggerHealth Logger::health() const noexcept {
    return LoggerHealth{
        .output_failures = shared_sink()->failure_count(),
        .last_error = shared_sink()->last_error(),
    };
}

std::size_t Logger::throttle_key_count() const {
    const std::scoped_lock lock{mutex_};
    return throttles_.size();
}

void Logger::write_line(const Severity severity, const std::string_view module, const LineContent content,
                        const std::uint64_t suppressed) {
    const auto now_time = std::chrono::system_clock::now();
    const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(now_time.time_since_epoch()).count();

    std::ostringstream line;
    if (!detailed_) {
        // Standard format matching: I/<module> (padded to 8 chars total) <message>
        line << level_char(severity) << '/' << module;
        if (module.size() < 8) {
            line << std::string(8 - module.size(), ' ');
        }
        line << ' ';
    } else {
        // Detailed format matching: I/<module> (padded) [<ISO time> pid:<pid> tid:<tid>] <message>
        line << level_char(severity) << '/' << module;
        if (module.size() < 8) {
            line << std::string(8 - module.size(), ' ');
        }
        line << " [";
        const auto time_c = std::chrono::system_clock::to_time_t(now_time);
        struct tm tm_buf{};
        if (::localtime_r(&time_c, &tm_buf) != nullptr) {
            char time_str[32];
            if (::strftime(time_str, sizeof(time_str), "%Y-%m-%dT%H:%M:%S", &tm_buf) > 0) {
                line << time_str;
            }
        }
        line << " pid:" << ::getpid() << " tid:" << ::syscall(SYS_gettid) << "] ";
    }

    line << "timestamp_unix_s=" << seconds << " severity=" << label(severity) << " event=\"" << content.event << '"';
    if (!content.context.empty()) {
        line << ' ' << content.context;
    }
    if (suppressed != 0) {
        line << " suppressed=" << suppressed;
    }
    line << '\n';

    auto formatted_str = line.str();
    constexpr std::string_view truncation_marker = " truncated=1\n";
    if (formatted_str.size() > max_record_bytes) {
        // Include prefixes and the newline in the collector's record/rotation budget.
        formatted_str.resize(max_record_bytes - truncation_marker.size());
        formatted_str.append(truncation_marker);
    }
    spdlog::details::log_msg msg(spdlog::source_loc{__FILE__, __LINE__, __func__},
                                 spdlog::string_view_t(module.data(), module.size()), spdlog_level(severity),
                                 spdlog::string_view_t(formatted_str.data(), formatted_str.size()));

    shared_sink()->log(msg);
}

} // namespace robot_control::service::logging
