#pragma once

#include "platform/linux/unique_fd.hpp"

#include <cstdint>
#include <string>
#include <sys/types.h>

namespace robot_control::application::ipc {
/** Copied control-owner observations, never an input to arbitration. Units are explicit on the wire. */
struct Snapshot {
    std::uint64_t cycles{0};
    unsigned source{0};
    int left_target_rpm{0};
    int right_target_rpm{0};
    int left_feedback_tenths_rpm{0};
    int right_feedback_tenths_rpm{0};
    std::uint32_t status_raw{0};
    std::uint32_t fault_raw{0};
    bool healthy{false};
    bool armed{false};
};

/**
 * Read-only Linux IPC endpoint. No command sample, CAN handle, thread or callback is retained.
 * All methods run on the control owner; callers must not share the instance across threads.
 */
class Server final {
  public:
    /**
     * Bind a nonblocking Unix SEQPACKET socket, or disable IPC when path is empty.
     * @param path Absolute socket path in a real directory owned by this UID with mode 0700.
     * Existing paths are never removed. The directory must remain private for this lifetime.
     * @param max_rpm Positive probe validation limit, at most 1000; grants no motion authority.
     * @throws std::system_error On setup failure; before any hardware activation.
     * @throws std::invalid_argument On invalid path/limit. Owns descriptors and its socket entry.
     */
    Server(std::string path, int max_rpm);
    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;
    /** Close descriptors and remove only this instance's socket inode; no control or device I/O. */
    ~Server();
    /**
     * Accept at most one peer and handle at most one bounded packet, without waiting or allocating.
     * @param state Borrowed immutable observation; copied into a response and never retained.
     * Only same-UID peers are accepted. Idle, failed or backpressured peers are disconnected.
     * Invalid probes are reported but never submitted to control policy. Owner thread only.
     */
    void poll(const Snapshot& state) noexcept;

  private:
    const std::string path_;
    const int max_rpm_;
    platform::linux::UniqueFd listener_{};
    platform::linux::UniqueFd peer_{};
    dev_t device_{0};
    ino_t inode_{0};
    std::uint64_t session_{0};
    std::uint64_t sequence_{0};
    std::int64_t started_ns_{0};
    std::int64_t last_request_ns_{0};
};
} // namespace robot_control::application::ipc
