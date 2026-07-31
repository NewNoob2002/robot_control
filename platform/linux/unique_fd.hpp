#pragma once

#include "platform/linux/error.hpp"

#include <unistd.h>

#include <utility>

namespace robot_control::platform::linux {

class UniqueFd final {
public:
  /** Construct an empty file-descriptor owner. */
  UniqueFd() noexcept = default;

  /**
   * Adopt one open file descriptor.
   *
   * @param fd Descriptor whose lifetime transfers to this object.
   */
  explicit UniqueFd(int fd) noexcept : fd_{fd} {}

  /** Close the owned descriptor, if any. */
  ~UniqueFd() { reset(); }

  UniqueFd(const UniqueFd &) = delete;
  UniqueFd &operator=(const UniqueFd &) = delete;

  /**
   * Move descriptor ownership from another object.
   *
   * @param other Source owner, left empty.
   */
  UniqueFd(UniqueFd &&other) noexcept : fd_{other.release()} {}

  /**
   * Replace this descriptor by moving ownership from another object.
   *
   * @param other Source owner, left empty.
   * @return This owner.
   */
  UniqueFd &operator=(UniqueFd &&other) noexcept {
    if (this != &other) {
      reset(other.release());
    }
    return *this;
  }

  /**
   * Return the borrowed raw descriptor.
   *
   * @return Descriptor or -1 when empty.
   */
  [[nodiscard]] int get() const noexcept { return fd_; }

  /**
   * Return whether a descriptor is owned.
   *
   * @return True when `get()` is nonnegative.
   */
  [[nodiscard]] explicit operator bool() const noexcept { return fd_ >= 0; }

  /**
   * Relinquish ownership without closing.
   *
   * @return Previously owned descriptor, or -1.
   */
  [[nodiscard]] int release() noexcept { return std::exchange(fd_, -1); }

  /**
   * Close the descriptor and report a close failure.
   *
   * The descriptor is relinquished even on error because retrying close can
   * accidentally close a descriptor reused by another thread.
   *
   * @return Success for an empty owner, otherwise the close status.
   */
  [[nodiscard]] Status close() noexcept {
    const int descriptor = release();
    if (descriptor < 0 || ::close(descriptor) == 0) {
      return Status::success();
    }
    return Status::from_errno("close", "fd=" + std::to_string(descriptor),
                              errno);
  }

  /**
   * Close the current descriptor and optionally adopt a replacement.
   *
   * Close errors are intentionally not reported from lifetime cleanup; explicit
   * I/O errors are reported by the owning adapter before reset.
   *
   * @param replacement Descriptor to adopt after cleanup.
   */
  void reset(int replacement = -1) noexcept {
    if (fd_ >= 0) {
      static_cast<void>(::close(fd_));
    }
    fd_ = replacement;
  }

private:
  int fd_{-1};
};

} // namespace robot_control::platform::linux
