#pragma once

#include "platform/linux/error.hpp"
#include "platform/linux/unique_fd.hpp"

#include <chrono>
#include <string>

#include <sys/types.h>

namespace robot_control::platform::linux::can {

/** Own a private fail-closed channel to the privileged CAN inhibitor. */
class InterfaceInhibitor final {
  public:
    using LaunchResult = Result<InterfaceInhibitor>;

    /**
     * Start the helper and wait until its independent CAN error monitor is ready.
     *
     * @param helper_path Helper executable path.
     * @param interface_name Interface identity; only can0 is accepted.
     * @param timeout Maximum startup acknowledgement interval.
     * @return Active guard or a context-rich failure.
     *
     * Thread safety: Call before creating application worker threads.
     */
    [[nodiscard]] static LaunchResult launch(std::string helper_path, std::string interface_name,
                                             std::chrono::milliseconds timeout) noexcept;

    /** Close the channel on an active guard, causing fail-closed inhibition. */
    ~InterfaceInhibitor();

    InterfaceInhibitor(const InterfaceInhibitor&) = delete;
    InterfaceInhibitor& operator=(const InterfaceInhibitor&) = delete;

    /** Transfer one active helper channel and child-process identity. */
    InterfaceInhibitor(InterfaceInhibitor&& other) noexcept;
    InterfaceInhibitor& operator=(InterfaceInhibitor&& other) = delete;

    /** Request immediate link inhibition and wait for verified link-down. */
    [[nodiscard]] Status inhibit(std::chrono::milliseconds timeout) noexcept;

    /** Wait for independent CAN-error monitoring to verify link-down. */
    [[nodiscard]] Status wait_inhibited(std::chrono::milliseconds timeout) noexcept;

    /** Disarm the helper only after application cleanup has been verified. */
    [[nodiscard]] Status release(std::chrono::milliseconds timeout) noexcept;

  private:
    /** Adopt one spawned helper channel and child-process identity. */
    InterfaceInhibitor(UniqueFd control, pid_t child, std::string context) noexcept;

    /** Send one command, receive its acknowledgement, and reap the helper. */
    [[nodiscard]] Status exchange(char command, char expected, std::chrono::milliseconds timeout,
                                  const char* operation) noexcept;

    /** Receive one exact protocol response before a monotonic deadline. */
    [[nodiscard]] Status receive(char expected, std::chrono::steady_clock::time_point deadline,
                                 const char* operation) noexcept;

    /** Close the protocol channel and verify a successful helper exit. */
    [[nodiscard]] Status finish(std::chrono::steady_clock::time_point deadline, const char* operation) noexcept;

    UniqueFd control_{};
    pid_t child_{-1};
    std::string context_{};
};

/**
 * Run the fixed-interface helper protocol on an inherited private socket.
 *
 * @param control_fd Inherited SOCK_SEQPACKET descriptor.
 * @param interface_name Interface identity; only can0 is accepted.
 * @return Zero after release or verified inhibition, nonzero on failure.
 *
 * Thread safety: Process main-thread only. Owns no caller memory.
 */
[[nodiscard]] int run_interface_inhibitor(int control_fd, const std::string& interface_name) noexcept;

} // namespace robot_control::platform::linux::can
