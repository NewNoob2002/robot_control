#include "application/ipc/server.hpp"

#include <array>
#include <charconv>
#include <cstdio>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <system_error>
#include <time.h>

namespace robot_control::application::ipc {
namespace {
/** Read the explicitly shared Linux clock; failure is invalid, never refreshed from wall time. */
std::int64_t monotonic_ns() noexcept {
    timespec now{};
    return ::clock_gettime(CLOCK_MONOTONIC, &now) == 0
               ? static_cast<std::int64_t>(now.tv_sec) * 1000000000LL + now.tv_nsec
               : 0;
}
/** Consume exactly one space-separated decimal field, rejecting overflow and empty fields. */
template <typename T> bool field(std::string_view& input, T& value) noexcept {
    const auto end = input.find(' ');
    const auto token = input.substr(0, end);
    if (token.empty())
        return false;
    const auto parsed = std::from_chars(token.data(), token.data() + token.size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr != token.data() + token.size())
        return false;
    input = end == std::string_view::npos ? std::string_view{} : input.substr(end + 1);
    return true;
}
} // namespace

Server::Server(std::string path, int max_rpm) : path_{std::move(path)}, max_rpm_{max_rpm} {
    if (path_.empty())
        return;
    sockaddr_un address{};
    if (path_.front() != '/' || path_.size() >= sizeof(address.sun_path) || path_.back() == '/'
        || path_.find('\0') != std::string::npos || max_rpm_ <= 0 || max_rpm_ > 1000)
        throw std::invalid_argument("invalid IPC socket path or probe limit");
    const auto parent = path_.substr(0, path_.find_last_of('/'));
    struct stat directory{};
    if (::lstat(parent.c_str(), &directory) != 0)
        throw std::system_error(errno, std::generic_category(), "IPC directory " + parent);
    if (!S_ISDIR(directory.st_mode) || directory.st_uid != ::geteuid() || (directory.st_mode & 0777) != 0700)
        throw std::system_error(EACCES, std::generic_category(), "IPC requires an owned 0700 directory: " + parent);
    started_ns_ = monotonic_ns();
    if (started_ns_ <= 0)
        throw std::system_error(EIO, std::generic_category(), "IPC monotonic clock " + path_);
    listener_.reset(::socket(AF_UNIX, SOCK_SEQPACKET | SOCK_NONBLOCK | SOCK_CLOEXEC, 0));
    if (!listener_)
        throw std::system_error(errno, std::generic_category(), "IPC socket " + path_);
    address.sun_family = AF_UNIX;
    std::memcpy(address.sun_path, path_.c_str(), path_.size() + 1);
    if (::bind(listener_.get(), reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0)
        throw std::system_error(errno, std::generic_category(), "IPC bind " + path_);
    struct stat entry{};
    if (::lstat(path_.c_str(), &entry) != 0)
        throw std::system_error(errno, std::generic_category(), "IPC stat " + path_);
    device_ = entry.st_dev;
    inode_ = entry.st_ino;
    if (::chmod(path_.c_str(), 0600) != 0 || ::listen(listener_.get(), 1) != 0) {
        const int error = errno;
        static_cast<void>(::unlink(path_.c_str()));
        throw std::system_error(error, std::generic_category(), "IPC listen " + path_);
    }
}

Server::~Server() {
    peer_.reset();
    listener_.reset();
    struct stat entry{};
    if (inode_ && ::lstat(path_.c_str(), &entry) == 0 && entry.st_dev == device_ && entry.st_ino == inode_)
        static_cast<void>(::unlink(path_.c_str()));
}

void Server::poll(const Snapshot& state) noexcept {
    if (!listener_)
        return;
    const auto now = monotonic_ns();
    if (now <= 0 || (peer_ && now - last_request_ns_ >= 1000000000LL))
        peer_.reset();
    if (now <= 0)
        return;
    platform::linux::UniqueFd incoming{::accept4(listener_.get(), nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC)};
    if (incoming && !peer_ && session_ < std::numeric_limits<std::uint64_t>::max()) {
        ucred credentials{};
        socklen_t size = sizeof(credentials);
        if (::getsockopt(incoming.get(), SOL_SOCKET, SO_PEERCRED, &credentials, &size) == 0
            && size == sizeof(credentials) && credentials.uid == ::geteuid()) {
            peer_ = std::move(incoming);
            ++session_;
            sequence_ = 0;
            last_request_ns_ = now;
        }
    }
    if (!peer_)
        return;
    std::array<char, 192> buffer{};
    iovec vector{buffer.data(), buffer.size()};
    msghdr message{};
    message.msg_iov = &vector;
    message.msg_iovlen = 1;
    const auto count = ::recvmsg(peer_.get(), &message, MSG_DONTWAIT | MSG_CMSG_CLOEXEC);
    if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR))
        return;
    if (count <= 0 || (message.msg_flags & (MSG_TRUNC | MSG_CTRUNC)) != 0) {
        peer_.reset();
        return;
    }
    last_request_ns_ = now;
    std::string_view input{buffer.data(), static_cast<std::size_t>(count)};
    std::uint64_t sequence = 0;
    std::int64_t captured_ns = 0;
    int kind = -1, left = 0, right = 0;
    const bool version = input.starts_with("RCI1 ");
    if (version)
        input.remove_prefix(5);
    const bool parsed = version && field(input, sequence) && field(input, captured_ns) && field(input, kind)
                        && field(input, left) && field(input, right) && input.empty()
                        && buffer[static_cast<std::size_t>(count) - 1] != ' ';
    const char* result = "probe_ok";
    if (!parsed || (kind != 0 && kind != 1) || (kind == 0 && (left != 0 || right != 0)))
        result = "malformed";
    else if (sequence == 0 || sequence <= sequence_)
        result = "sequence_replay";
    else {
        sequence_ = sequence;
        if (captured_ns <= 0 || captured_ns > now || now - captured_ns >= 100000000LL)
            result = "stale_or_future";
        else if (left < -max_rpm_ || left > max_rpm_ || right < -max_rpm_ || right > max_rpm_)
            result = "rpm_range";
        else if (kind == 0)
            result = "status";
    }
    std::array<char, 1024> response{};
    const int length = std::snprintf(
        response.data(), response.size(),
        "{\"version\":1,\"server_started_ns\":%lld,\"session\":%llu,\"sequence\":%llu,"
        "\"captured_ns\":%lld,\"result\":\"%s\",\"executed\":false,\"motion_authority\":false,"
        "\"cycles\":%llu,\"source\":%u,\"left_target_rpm\":%d,\"right_target_rpm\":%d,"
        "\"left_feedback_tenths_rpm\":%d,\"right_feedback_tenths_rpm\":%d,"
        "\"status_raw\":%u,\"fault_raw\":%u,\"healthy\":%s,\"armed\":%s}",
        static_cast<long long>(started_ns_), static_cast<unsigned long long>(session_),
        static_cast<unsigned long long>(sequence), static_cast<long long>(now), result,
        static_cast<unsigned long long>(state.cycles), state.source, state.left_target_rpm, state.right_target_rpm,
        state.left_feedback_tenths_rpm, state.right_feedback_tenths_rpm, state.status_raw, state.fault_raw,
        state.healthy ? "true" : "false", state.armed ? "true" : "false");
    if (length <= 0 || static_cast<std::size_t>(length) >= response.size()) {
        peer_.reset();
        return;
    }
    vector = {response.data(), static_cast<std::size_t>(length)};
    message = {};
    message.msg_iov = &vector;
    message.msg_iovlen = 1;
    if (::sendmsg(peer_.get(), &message, MSG_DONTWAIT | MSG_NOSIGNAL) != length)
        peer_.reset(); // Slow observers must never block the control owner or create an unbounded reply queue.
}
} // namespace robot_control::application::ipc
