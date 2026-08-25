#pragma once

#include "communication/canopen/stack_config.hpp"

extern "C" {
#include "CANopen.h"
#include "OD.h"
}

#include <cstdint>
#include <memory>

namespace robot_control::communication::canopen {

/**
 * Own the sole process-wide CANopen allocation backed by the generated OD.
 *
 * The generated dictionary exposes mutable globals, so simultaneous contexts
 * are rejected. Destruction frees upstream heap storage before releasing the
 * claim. This class performs allocation only; it never opens or configures CAN.
 */
class StackStorage final {
public:
  using CreateResult = platform::linux::Result<std::unique_ptr<StackStorage>>;

  /**
   * Validate configuration and allocate one inactive CANopen stack context.
   *
   * @param config Injected startup values transferred to the new owner.
   * @return Unique ownership on success, or validation, busy, or allocation
   * failure with both node identities in ownership/allocation error context.
   *
   * Thread safety: Process-wide atomic exclusion permits one live instance.
   * The returned upstream pointers remain owned by that instance.
   */
  [[nodiscard]] static CreateResult create(StackConfig config) noexcept;

  /** Clear OD extensions, free allocation, then release the ownership claim. */
  ~StackStorage();

  StackStorage(const StackStorage &) = delete;
  StackStorage &operator=(const StackStorage &) = delete;
  StackStorage(StackStorage &&) = delete;
  StackStorage &operator=(StackStorage &&) = delete;

  /**
   * Access the inactive upstream stack.
   *
   * @return Mutable pointer owned by this instance.
   *
   * Thread safety: Owner-thread only; lifetime ends with this instance.
   */
  [[nodiscard]] CO_t *stack() noexcept;

  /**
   * Access the fixed generated object dictionary.
   *
   * @return Mutable process-global dictionary pointer claimed by this instance.
   *
   * Thread safety: Owner-thread only; do not retain after destruction.
   */
  [[nodiscard]] OD_t *object_dictionary() noexcept;

  /**
   * Inspect the explicit active upstream object counts.
   *
   * @return Immutable configuration owned by this instance.
   *
   * Thread safety: Safe for concurrent immutable reads while this lives.
   */
  [[nodiscard]] const CO_config_t &upstream_config() const noexcept;

  /**
   * Report the heap bytes counted by the pinned CO_new implementation.
   *
   * @return Stable nonzero allocation size after successful creation.
   *
   * Thread safety: Safe for concurrent immutable reads while this lives.
   */
  [[nodiscard]] std::uint32_t heap_memory_used() const noexcept;

  /**
   * Inspect the validated injected startup values.
   *
   * @return Immutable configuration owned by this instance.
   *
   * Thread safety: Safe for concurrent immutable reads while this lives.
   */
  [[nodiscard]] const StackConfig &config() const noexcept;

private:
  /**
   * Construct claimed storage before upstream allocation.
   *
   * @param config Validated startup values transferred into this owner.
   *
   * Thread safety: Caller must already own the process-wide claim.
   */
  explicit StackStorage(StackConfig config) noexcept;

  StackConfig config_{};
  CO_config_t upstream_config_{};
  CO_t *stack_{nullptr};
  std::uint32_t heap_memory_used_{0};
};

} // namespace robot_control::communication::canopen
