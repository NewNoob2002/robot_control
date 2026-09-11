#include "communication/canopen/lifecycle.hpp"

#include <linux/can.h>
#include <linux/can/error.h>
#include <linux/can/raw.h>
#include <net/if.h>
#include <sys/epoll.h>
#include <sys/ioctl.h>
#include <sys/socket.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <csignal>
#include <cstdint>
#include <limits>
#include <new>
#include <string>
#include <utility>

namespace robot_control::communication::canopen {
namespace {

constexpr std::uint32_t process_interval_us = 1'000U;
constexpr auto interface_check_interval = std::chrono::milliseconds{10};
constexpr canid_t exact_standard_filter_mask = CAN_SFF_MASK | CAN_EFF_FLAG | CAN_RTR_FLAG;

/** Pinned error details kept together to prevent argument-order mistakes. */
struct UpstreamFailure {
    CO_ReturnError_t error;
    std::uint32_t error_info;
    int error_number;
};

/** Format stable interface and node identity for every lifecycle failure. */
std::string identity_context(const StackConfig& config) {
    return "interface=" + config.interface_name + " controller=" + std::to_string(config.controller_node_id)
           + " remote=" + std::to_string(config.remote_node_id);
}

/** Convert a pinned upstream failure into the project status contract. */
platform::linux::Status upstream_status(const char* operation, const StackConfig& config,
                                        const UpstreamFailure failure) {
    int error_number = failure.error_number;
    if (error_number == 0) {
        error_number = failure.error == CO_ERROR_OUT_OF_MEMORY ? ENOMEM : EPROTO;
    }
    return platform::linux::Status::from_errno(operation,
                                               identity_context(config)
                                                   + " upstream=" + std::to_string(static_cast<int>(failure.error))
                                                   + " err_info=" + std::to_string(failure.error_info),
                                               error_number);
}

/** Convert one Linux Classical CAN frame into the raw observation contract. */
RawCanopenFrame raw_frame(const can_frame& frame, const std::chrono::steady_clock::time_point received_at,
                          const ObservationGeneration generation) noexcept {
    RawCanopenFrame raw{
        .identifier = frame.can_id,
        .dlc = frame.can_dlc,
        .received_at = received_at,
        .generation = generation,
    };
    std::copy_n(frame.data, raw.payload.size(), raw.payload.begin());
    return raw;
}

/** Return whether the pinned driver copied the same matched frame that was peeked. */
bool same_frame(const can_frame& peeked, const CO_CANrxMsg_t& consumed) noexcept {
    return consumed.ident == peeked.can_id && consumed.DLC == peeked.can_dlc
           && std::equal(std::begin(consumed.data), std::end(consumed.data), std::begin(peeked.data));
}

/** Require one named CAN interface to remain administratively up. */
platform::linux::Status require_interface_up(const int descriptor, const StackConfig& config) noexcept {
    ifreq request{};
    std::copy(config.interface_name.begin(), config.interface_name.end(), request.ifr_name);
    errno = 0;
    if (::ioctl(descriptor, SIOCGIFFLAGS, &request) != 0) {
        return platform::linux::Status::from_errno("ioctl(SIOCGIFFLAGS)", identity_context(config), errno);
    }
    if ((request.ifr_flags & IFF_UP) == 0) {
        return platform::linux::Status::from_errno("ioctl(SIOCGIFFLAGS)", identity_context(config) + " state=down",
                                                   ENETDOWN);
    }
    return platform::linux::Status::success();
}

/** Install the exact one-peer receive set needed by the observation owner. */
platform::linux::Status install_observation_filters(const int descriptor, const StackConfig& config) noexcept {
    const std::array<can_filter, 8> filters{{
        {.can_id = 0x000U, .can_mask = exact_standard_filter_mask},
        {.can_id = 0x080U + config.remote_node_id, .can_mask = exact_standard_filter_mask},
        {.can_id = 0x180U + config.remote_node_id, .can_mask = exact_standard_filter_mask},
        {.can_id = 0x280U + config.remote_node_id, .can_mask = exact_standard_filter_mask},
        {.can_id = 0x380U + config.remote_node_id, .can_mask = exact_standard_filter_mask},
        {.can_id = 0x480U + config.remote_node_id, .can_mask = exact_standard_filter_mask},
        {.can_id = 0x580U + config.remote_node_id, .can_mask = exact_standard_filter_mask},
        {.can_id = 0x700U + config.remote_node_id, .can_mask = exact_standard_filter_mask},
    }};
    errno = 0;
    if (::setsockopt(descriptor, SOL_CAN_RAW, CAN_RAW_FILTER, filters.data(), sizeof(filters)) != 0) {
        return platform::linux::Status::from_errno("setsockopt(CAN_RAW_FILTER)", identity_context(config), errno);
    }
    return platform::linux::Status::success();
}

} // namespace

Lifecycle::Lifecycle(std::unique_ptr<StackStorage> storage,
                     platform::linux::process::TerminationEvent& termination) noexcept
    : storage_{std::move(storage)}, observations_{storage_->config()}, termination_{&termination} {
    epoll_.epoll_fd = -1;
    epoll_.event_fd = -1;
    epoll_.timer_fd = -1;
}

Lifecycle::CreateResult Lifecycle::create(StackConfig config,
                                          platform::linux::process::TerminationEvent& termination) noexcept {
    auto storage_result = StackStorage::create(std::move(config));
    if (!storage_result.ok()) {
        return CreateResult::failure(storage_result.status());
    }
    auto storage = std::move(storage_result).value();
    const auto context = identity_context(storage->config());

    auto owner = std::unique_ptr<Lifecycle>{new (std::nothrow) Lifecycle(std::move(storage), termination)};
    if (!owner) {
        return CreateResult::failure(
            platform::linux::Status::from_errno("allocate_canopen_lifecycle", context, ENOMEM));
    }

    errno = 0;
    const auto epoll_error = CO_epoll_create(&owner->epoll_, process_interval_us);
    if (epoll_error != CO_ERROR_NO) {
        const int saved_errno = errno;
        return CreateResult::failure(
            upstream_status("CO_epoll_create", owner->storage_->config(),
                            {.error = epoll_error, .error_info = 0U, .error_number = saved_errno}));
    }

    epoll_event event{};
    event.events = EPOLLIN;
    event.data.fd = owner->termination_->fd();
    if (::epoll_ctl(owner->epoll_.epoll_fd, EPOLL_CTL_ADD, event.data.fd, &event) != 0) {
        return CreateResult::failure(
            platform::linux::Status::from_errno("epoll_ctl", context + " resource=termination_signalfd", errno));
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
    observations_.begin_transport();
    storage_->prepare_communication_reset();
    const auto& config = storage_->config();
    const auto context = identity_context(config);

    errno = 0;
    const unsigned int interface_index = ::if_nametoindex(config.interface_name.c_str());
    if (interface_index == 0U) {
        return platform::linux::Status::from_errno("if_nametoindex", context + " upstream=not-called",
                                                   errno == 0 ? ENODEV : errno);
    }

    CO_CANptrSocketCan_t endpoint{
        .can_ifindex = static_cast<int>(interface_index),
        .epoll_fd = epoll_.epoll_fd,
    };

    errno = 0;
    auto error = CO_CANinit(storage_->stack(), &endpoint, config.bit_rate_kbit_s);
    if (error != CO_ERROR_NO) {
        const int saved_errno = errno;
        storage_->prepare_communication_reset();
        return upstream_status("CO_CANinit", config, {.error = error, .error_info = 0U, .error_number = saved_errno});
    }

    auto* const module = storage_->stack()->CANmodule;
    if (module->CANinterfaceCount != 1U) {
        storage_->prepare_communication_reset();
        return platform::linux::Status::from_errno("CO_CANinit", context + " interface_count", EPROTO);
    }
    auto interface_status = require_interface_up(module->CANinterfaces[0].fd, config);
    if (!interface_status.ok()) {
        storage_->prepare_communication_reset();
        return interface_status;
    }
    constexpr can_err_mask_t error_mask = CAN_ERR_MASK;
    errno = 0;
    if (::setsockopt(module->CANinterfaces[0].fd, SOL_CAN_RAW, CAN_RAW_ERR_FILTER, &error_mask, sizeof(error_mask))
        != 0) {
        const int saved_errno = errno;
        storage_->prepare_communication_reset();
        return platform::linux::Status::from_errno("setsockopt(CAN_RAW_ERR_FILTER)", context, saved_errno);
    }

    std::uint32_t error_info = 0U;
    errno = 0;
    error = CO_CANopenInit(storage_->stack(), nullptr, nullptr, storage_->object_dictionary(), nullptr, 0U, 0U, 0U,
                           static_cast<std::uint16_t>(config.sdo_timeout.count()), false, config.controller_node_id,
                           &error_info);
    if (error != CO_ERROR_NO) {
        const int saved_errno = errno;
        storage_->prepare_communication_reset();
        return upstream_status("CO_CANopenInit", config,
                               {.error = error, .error_info = error_info, .error_number = saved_errno});
    }

    // This local client/observer is not an announcing NMT slave. Upstream sends
    // boot-up from INITIALIZING even with producer heartbeat disabled. Establish
    // the silent local role on every reset; remote observations remain separate.
    storage_->stack()->NMT->operatingState = CO_NMT_PRE_OPERATIONAL;
    storage_->stack()->NMT->operatingStatePrev = CO_NMT_PRE_OPERATIONAL;
    CO_epoll_initCANopenMain(&epoll_, storage_->stack());

    error_info = 0U;
    errno = 0;
    error = CO_CANopenInitPDO(storage_->stack(), storage_->stack()->em, storage_->object_dictionary(),
                              config.controller_node_id, &error_info);
    if (error != CO_ERROR_NO) {
        const int saved_errno = errno;
        storage_->prepare_communication_reset();
        return upstream_status("CO_CANopenInitPDO", config,
                               {.error = error, .error_info = error_info, .error_number = saved_errno});
    }

    errno = 0;
    CO_CANsetNormalMode(storage_->stack()->CANmodule);
    if (!storage_->stack()->CANmodule->CANnormal) {
        const int saved_errno = errno;
        storage_->prepare_communication_reset();
        return platform::linux::Status::from_errno("CO_CANsetNormalMode", context + " upstream=void",
                                                   saved_errno == 0 ? EIO : saved_errno);
    }
    auto filter_status = install_observation_filters(module->CANinterfaces[0].fd, config);
    if (!filter_status.ok()) {
        storage_->prepare_communication_reset();
        return filter_status;
    }
    next_interface_check_ = std::chrono::steady_clock::now() + interface_check_interval;
    return platform::linux::Status::success();
}

bool Lifecycle::endpoint_lost() const noexcept {
    const auto* module = storage_->stack()->CANmodule;
    return epoll_.epoll_new && module->CANinterfaceCount == 1U && epoll_.ev.data.fd == module->CANinterfaces[0].fd
           && (epoll_.ev.events & static_cast<std::uint32_t>(EPOLLERR | EPOLLHUP)) != 0U;
}

platform::linux::Status
Lifecycle::process_receive_event(const std::chrono::steady_clock::time_point received_at) noexcept {
    auto* const module = storage_->stack()->CANmodule;
    if (!epoll_.epoll_new || module->CANinterfaceCount != 1U || epoll_.ev.data.fd != module->CANinterfaces[0].fd
        || (epoll_.ev.events & static_cast<std::uint32_t>(EPOLLIN)) == 0U) {
        return platform::linux::Status::success();
    }

    const auto context = identity_context(storage_->config());
    can_frame peeked{};
    errno = 0;
    const auto received = ::recv(epoll_.ev.data.fd, &peeked, sizeof(peeked), MSG_PEEK | MSG_DONTWAIT);
    const int receive_error = errno;
    if (received < 0 && (receive_error == EINTR || receive_error == EAGAIN || receive_error == EWOULDBLOCK)) {
        // Return to the owner loop so deadlines/signals still run; do not consume or publish a frame.
        epoll_.epoll_new = false;
        return platform::linux::Status::success();
    }
    if (received != static_cast<ssize_t>(sizeof(peeked))) {
        return platform::linux::Status::from_errno("recv(MSG_PEEK)", context + " resource=can_frame",
                                                   received < 0 ? receive_error : EIO);
    }

    CO_CANrxMsg_t consumed{};
    constexpr auto unset_index = std::numeric_limits<std::int32_t>::min();
    std::int32_t message_index = unset_index;
    if (!CO_CANrxFromEpoll(module, &epoll_.ev, &consumed, &message_index)) {
        return platform::linux::Status::from_errno("CO_CANrxFromEpoll", context + " event_fd_mismatch", EPROTO);
    }
    epoll_.epoll_new = false;

    if ((peeked.can_id & CAN_ERR_FLAG) == 0U) {
        if (message_index == unset_index) {
            return platform::linux::Status::from_errno("CO_CANrxFromEpoll", context + " receive_result_missing", EIO);
        }
        if (message_index >= 0 && !same_frame(peeked, consumed)) {
            return platform::linux::Status::from_errno("CO_CANrxFromEpoll", context + " peek_consume_mismatch", EPROTO);
        }
    }

    observations_.ingest(raw_frame(peeked, received_at, observations_.generation()), received_at);
    return platform::linux::Status::success();
}

Lifecycle::RunResult Lifecycle::run_until(const std::chrono::steady_clock::time_point deadline) noexcept {
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
            // CAN registration is level-triggered: leave the frame queued for
            // the next run, rather than report an unprocessed event as unknown.
            epoll_.epoll_new = false;
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

        const auto interface_check_time = std::chrono::steady_clock::now();
        if (interface_check_time >= next_interface_check_) {
            next_interface_check_ = interface_check_time + interface_check_interval;
            const auto* const module = storage_->stack()->CANmodule;
            const auto interface_status =
                module->CANinterfaceCount == 1U
                    ? require_interface_up(module->CANinterfaces[0].fd, storage_->config())
                    : platform::linux::Status::from_errno("check_can_interface", identity_context(storage_->config()),
                                                          EPROTO);
            if (!interface_status.ok()) {
                epoll_.epoll_new = false;
                CO_epoll_processLast(&epoll_);
                const auto status = reopen();
                if (!status.ok()) {
                    return RunResult::failure(status);
                }
                continue;
            }
        }

        const auto receive_status = process_receive_event(std::chrono::steady_clock::now());
        if (!receive_status.ok()) {
            CO_epoll_processLast(&epoll_);
            return RunResult::failure(receive_status);
        }
        observations_.advance_time(std::chrono::steady_clock::now());

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

ObservationSnapshot Lifecycle::observation_snapshot(const std::chrono::steady_clock::time_point now) const noexcept {
    return observations_.snapshot(now);
}

} // namespace robot_control::communication::canopen
