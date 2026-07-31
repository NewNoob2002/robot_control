#pragma once

#include <cerrno>
#include <optional>
#include <string>
#include <system_error>
#include <utility>

namespace robot_control::platform::linux {

struct Status {
  std::string operation{};
  std::string context{};
  std::error_code error{};

  /**
   * Return whether the operation completed without an error.
   *
   * @return True when the stored error code is clear.
   *
   * Thread safety: Safe for concurrent reads of an immutable instance.
   */
  [[nodiscard]] bool ok() const noexcept { return !error; }

  /**
   * Construct a successful status.
   *
   * @return Status with no error code.
   *
   * Thread safety: Pure and reentrant.
   */
  [[nodiscard]] static Status success() { return {}; }

  /**
   * Capture a POSIX errno failure with operation and resource context.
   *
   * @param operation Operation that failed.
   * @param context Device, path, or resource identity.
   * @param error_number Captured errno value.
   * @return Context-rich failure status.
   *
   * Thread safety: Pure and reentrant.
   */
  [[nodiscard]] static Status
  from_errno(std::string operation, std::string context, int error_number) {
    return Status{
        .operation = std::move(operation),
        .context = std::move(context),
        .error = {error_number, std::generic_category()},
    };
  }
};

template <typename T> class Result final {
public:
  /**
   * Construct a successful result owning a value.
   *
   * @param value Value transferred into the result.
   * @return Successful result.
   */
  [[nodiscard]] static Result success(T value) {
    return Result{std::move(value), Status::success()};
  }

  /**
   * Construct a failed result without a value.
   *
   * @param status Context-rich failure status.
   * @return Failed result.
   */
  [[nodiscard]] static Result failure(Status status) {
    return Result{std::nullopt, std::move(status)};
  }

  /**
   * Return whether a value is available.
   *
   * @return True only for a successful result.
   */
  [[nodiscard]] bool ok() const noexcept { return value_.has_value(); }

  /**
   * Access the immutable operation status.
   *
   * @return Status reference with lifetime tied to this result.
   */
  [[nodiscard]] const Status &status() const noexcept { return status_; }

  /**
   * Access the owned value.
   *
   * @return Mutable value reference with lifetime tied to this result.
   *
   * Precondition: `ok()` is true.
   */
  [[nodiscard]] T &value() & noexcept { return *value_; }

  /**
   * Access the immutable owned value.
   *
   * @return Const value reference with lifetime tied to this result.
   *
   * Precondition: `ok()` is true.
   */
  [[nodiscard]] const T &value() const & noexcept { return *value_; }

  /**
   * Transfer the owned value out of an rvalue result.
   *
   * @return Moved value.
   *
   * Precondition: `ok()` is true.
   */
  [[nodiscard]] T &&value() && noexcept { return std::move(*value_); }

private:
  Result(std::optional<T> value, Status status)
      : value_{std::move(value)}, status_{std::move(status)} {}

  Result(T value, Status status)
      : value_{std::move(value)}, status_{std::move(status)} {}

  std::optional<T> value_{};
  Status status_{};
};

} // namespace robot_control::platform::linux
