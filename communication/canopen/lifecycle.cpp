#include "communication/canopen/lifecycle.hpp"

#include <net/if.h>
#include <sys/epoll.h>

#include <cerrno>
#include <csignal>
#include <cstdint>
#include <new>
#include <string>
#include <utility>

namespace robot_control::communication::canopen {
namespace {

constexpr std::uint32_t process_interval_us = 1'000U;

/** Format stable interface and node identity for every lifecycle failure. */
std::string identity_context(const StackConfig &config) {
  return "interface=" + config.interface_name +
         " controller=" + std::to_string(config.controller_node_id) +
         " remote=" + std::to_string(config.remote_node_id);
}

/** Convert a pinned upstream failure into the project status contract. */
platform::linux::Status upstream_status(const char *operation,
                                        const StackConfig &config,
                                        const CO_ReturnError_t upstream_error,
                                        const std::uint32_t error_info,
                                        const int saved_errno) {
  int error_number = saved_errno;
  if (error_number == 0) {
    error_number = upstream_error == CO_ERROR_OUT_OF_MEMORY ? ENOMEM : EPROTO;
  }
  return platform::linux::Status::from_errno(
      operation, identity_context(config) +
                     " upstream=" +
                     std::to_string(static_cast<int>(upstream_error)) +
                     " err_info=" + std::to_string(error_info),
      error_number);
}

} // namespace

Lifecycle::Lifecycle(
    std::unique_ptr<StackStorage> storage,
    platform::linux::process::TerminationEvent &termination) noexcept
    : storage_{std::move(storage)}, termination_{&termination} {
  epoll_.epoll_fd = -1;
  epoll_.event_fd = -1;
  epoll_.timer_fd = -1;
}

Lifecycle::CreateResult Lifecycle::create(
    StackConfig config,
    platform::linux::process::TerminationEvent &termination) noexcept {
  auto storage_result = StackStorage::create(std::move(config));
  if (!storage_result.ok()) {
    return CreateResult::failure(storage_result.status());
  }
  auto storage = std::move(storage_result).value();
  const auto context = identity_context(storage->config());

  auto owner = std::unique_ptr<Lifecycle>{new (std::nothrow) Lifecycle(
      std::move(storage), termination)};
  if (!owner) {
    return CreateResult::failure(platform::linux::Status::from_errno(
        "allocate_canopen_lifecycle", context, ENOMEM));
  }

  errno = 0;
  const auto epoll_error =
      CO_epoll_create(&owner->epoll_, process_interval_us);
  if (epoll_error != CO_ERROR_NO) {
    const int saved_errno = errno;
    return CreateResult::failure(upstream_status(
        "CO_epoll_create", owner->storage_->config(), epoll_error, 0U,
        saved_errno));
  }

  epoll_event event{};
  event.events = EPOLLIN;
  event.data.fd = owner->termination_->fd();
  if (::epoll_ctl(owner->epoll_.epoll_fd, EPOLL_CTL_ADD, event.data.fd,
                  &event) != 0) {
    return CreateResult::failure(platform::linux::Status::from_errno(
        "epoll_ctl", context + " resource=termination_signalfd", errno));
  }

  const auto open_status = owner->reopen();
  if (!open_status.ok()) {
    return CreateResult::failure(open_status);
  }
  return CreateResult::success(std::move(owner));
}

Lifecycle::~Lifecycle() {
  if (storage_) {
    storage_->prepare_communication_reset();
  }
  if (epoll_.epoll_fd >= 0 || epoll_.event_fd >= 0 || epoll_.timer_fd >= 0) {
    CO_epoll_close(&epoll_);
  }
}

platform::linux::Status Lifecycle::reopen() noexcept {
  storage_->prepare_communication_reset();
  const auto &config = storage_->config();
  const auto context = identity_context(config);

  errno = 0;
  const unsigned int interface_index =
      ::if_nametoindex(config.interface_name.c_str());
  if (interface_index == 0U) {
    return platform::linux::Status::from_errno(
        "if_nametoindex", context + " upstream=not-called",
        errno == 0 ? ENODEV : errno);
  }

  CO_CANptrSocketCan_t endpoint{
      .can_ifindex = static_cast<int>(interface_index),
      .epoll_fd = epoll_.epoll_fd,
  };

  errno = 0;
  auto error =
      CO_CANinit(storage_->stack(), &endpoint, config.bit_rate_kbit_s);
  if (error != CO_ERROR_NO) {
    const int saved_errno = errno;
    storage_->prepare_communication_reset();
    return upstream_status("CO_CANinit", config, error, 0U, saved_errno);
  }

  std::uint32_t error_info = 0U;
  errno = 0;
  error = CO_CANopenInit(
      storage_->stack(), nullptr, nullptr, storage_->object_dictionary(),
      nullptr, 0U, 0U, 0U,
      static_cast<std::uint16_t>(config.sdo_timeout.count()), false,
      config.controller_node_id, &error_info);
  if (error != CO_ERROR_NO) {
    const int saved_errno = errno;
    storage_->prepare_communication_reset();
    return upstream_status("CO_CANopenInit", config, error, error_info,
                           saved_errno);
  }

  CO_epoll_initCANopenMain(&epoll_, storage_->stack());

  error_info = 0U;
  errno = 0;
  error = CO_CANopenInitPDO(storage_->stack(), storage_->stack()->em,
                            storage_->object_dictionary(),
                            config.controller_node_id, &error_info);
  if (error != CO_ERROR_NO) {
    const int saved_errno = errno;
    storage_->prepare_communication_reset();
    return upstream_status("CO_CANopenInitPDO", config, error, error_info,
                           saved_errno);
  }

  errno = 0;
  CO_CANsetNormalMode(storage_->stack()->CANmodule);
  if (!storage_->stack()->CANmodule->CANnormal) {
    const int saved_errno = errno;
    storage_->prepare_communication_reset();
    return platform::linux::Status::from_errno(
        "CO_CANsetNormalMode", context + " upstream=void",
        saved_errno == 0 ? EIO : saved_errno);
  }
  return platform::linux::Status::success();
}

bool Lifecycle::endpoint_lost() const noexcept {
  const auto *module = storage_->stack()->CANmodule;
  return epoll_.epoll_new && module->CANinterfaceCount == 1U &&
         epoll_.ev.data.fd == module->CANinterfaces[0].fd &&
         (epoll_.ev.events & static_cast<std::uint32_t>(EPOLLERR | EPOLLHUP)) !=
             0U;
}

Lifecycle::RunResult Lifecycle::run_until(
    const std::chrono::steady_clock::time_point deadline) noexcept {
  while (true) {
    if (std::chrono::steady_clock::now() >= deadline) {
      return RunResult::success(LifecycleExit::deadline);
    }

    CO_epoll_wait(&epoll_);

    if (epoll_.epoll_new && epoll_.ev.data.fd == termination_->fd()) {
      epoll_.epoll_new = false;
      CO_epoll_processLast(&epoll_);
      auto signal = termination_->consume();
      if (!signal.ok()) {
        return RunResult::failure(signal.status());
      }
      if (signal.value() == SIGINT) {
        return RunResult::success(LifecycleExit::sigint);
      }
      if (signal.value() == SIGTERM) {
        return RunResult::success(LifecycleExit::sigterm);
      }
    }

    if (std::chrono::steady_clock::now() >= deadline) {
      CO_epoll_processLast(&epoll_);
      return RunResult::success(LifecycleExit::deadline);
    }

    if (endpoint_lost()) {
      epoll_.epoll_new = false;
      CO_epoll_processLast(&epoll_);
      const auto status = reopen();
      if (!status.ok()) {
        return RunResult::failure(status);
      }
      continue;
    }

    CO_epoll_processRT(&epoll_, storage_->stack(), false);
    CO_NMT_reset_cmd_t reset = CO_RESET_NOT;
    CO_epoll_processMain(&epoll_, storage_->stack(), false, &reset);
    CO_epoll_processLast(&epoll_);

    if (reset == CO_RESET_COMM) {
      const auto status = reopen();
      if (!status.ok()) {
        return RunResult::failure(status);
      }
    } else if (reset == CO_RESET_APP) {
      return RunResult::success(LifecycleExit::application_reset);
    } else if (reset == CO_RESET_QUIT) {
      return RunResult::success(LifecycleExit::quit);
    }
  }
}

} // namespace robot_control::communication::canopen
