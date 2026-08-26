#pragma once

#include "communication/canopen/lifecycle.hpp"

#include <cstdint>

namespace robot_control::communication::canopen {

/** Motion-inhibiting NMT commands available to the commissioning tool. */
enum class CommissioningNmt : std::uint8_t { stopped = 0x02U, pre_operational = 0x80U };

/** Execute one-at-a-time, node-1 read-only commissioning operations. */
class CommissioningSession final {
  public:
    /**
     * Borrow one active lifecycle owner.
     *
     * @param lifecycle Owner retained by reference for this session lifetime.
     *
     * Thread safety: Construct and use only on the lifecycle owner thread.
     */
    explicit CommissioningSession(Lifecycle& lifecycle) noexcept;

    /**
     * Send one reviewed motion-inhibiting NMT transition to node 1.
     *
     * @param command Stopped or Pre-operational command.
     * @return Success after one kernel submission, otherwise a contextual failure.
     *
     * Thread safety: Lifecycle owner thread only.
     */
    [[nodiscard]] platform::linux::Status send_nmt(CommissioningNmt command) noexcept;

    /**
     * Upload one exact whitelisted object, with at most one explicit retry.
     *
     * @param index Whitelisted object dictionary index.
     * @param subindex Whitelisted object dictionary subindex.
     * @param retry_once True to permit one timeout retry after quarantine.
     * @return Correlated expedited data or server abort, otherwise a failure.
     *
     * Thread safety: Lifecycle owner thread only; requests must not overlap.
     */
    [[nodiscard]] platform::linux::Result<SdoObservation> upload(std::uint16_t index, std::uint8_t subindex,
                                                                 bool retry_once) noexcept;

  private:
    /** Return success only for fixed node 1 with current boot and heartbeat evidence. */
    [[nodiscard]] platform::linux::Status require_fresh_remote() const noexcept;

    Lifecycle* lifecycle_{nullptr};
    std::uint64_t request_generation_{0};
    std::uint64_t attempt_generation_{0};
};

} // namespace robot_control::communication::canopen
