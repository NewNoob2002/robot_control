#include "communication/canopen/stack_storage.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <new>
#include <string>
#include <utility>

namespace robot_control::communication::canopen {
namespace {

std::atomic_flag stack_claim = ATOMIC_FLAG_INIT;

/** Return stable node identity context for allocation and ownership errors. */
std::string identity_context(const StackConfig &config) {
  return "controller=" + std::to_string(config.controller_node_id) +
         " remote=" + std::to_string(config.remote_node_id);
}

/** Restore all generated mutable OD groups before applying injected values. */
void reset_generated_dictionary() noexcept {
  static const OD_PERSIST_COMM_t generated_persist_comm = OD_PERSIST_COMM;
  static const OD_RAM_t generated_ram = OD_RAM;
  OD_PERSIST_COMM = generated_persist_comm;
  OD_RAM = generated_ram;
}

/** Apply the one-remote-node receive configuration without stack activation. */
void apply_receive_configuration(const StackConfig &config) noexcept {
  OD_PERSIST_COMM.x1017_producerHeartbeatTime = 0U;
  OD_PERSIST_COMM.x1016_consumerHeartbeatTime_sub0 = 1U;
  std::fill(std::begin(OD_PERSIST_COMM.x1016_consumerHeartbeatTime),
            std::end(OD_PERSIST_COMM.x1016_consumerHeartbeatTime), 0U);
  OD_PERSIST_COMM.x1016_consumerHeartbeatTime[0] =
      (static_cast<std::uint32_t>(config.remote_node_id) << 16U) |
      static_cast<std::uint32_t>(config.heartbeat_timeout.count());

  auto &sdo = OD_PERSIST_COMM.x1280_SDOClientParameter;
  sdo.COB_IDClientToServerTx = 0x600U + config.remote_node_id;
  sdo.COB_IDServerToClientRx = 0x580U + config.remote_node_id;
  sdo.node_IDOfTheSDOServer = config.remote_node_id;

  const std::array<std::uint32_t, 4> cob_ids{
      0x180U + config.remote_node_id, 0x280U + config.remote_node_id,
      0x380U + config.remote_node_id, 0x480U + config.remote_node_id};
  OD_PERSIST_COMM.x1400_RPDOCommunicationParameter.COB_IDUsedByRPDO =
      cob_ids[0];
  OD_PERSIST_COMM.x1401_RPDOCommunicationParameter.COB_IDUsedByRPDO =
      cob_ids[1];
  OD_PERSIST_COMM.x1402_RPDOCommunicationParameter.COB_IDUsedByRPDO =
      cob_ids[2];
  OD_PERSIST_COMM.x1403_RPDOCommunicationParameter.COB_IDUsedByRPDO =
      cob_ids[3];
}

/** Narrow generated dictionary counts to the reviewed P5.2 feature subset. */
void configure_active_counts(CO_config_t &config) noexcept {
  OD_INIT_CONFIG(config);
  config.CNT_NMT = 1U;
  config.CNT_HB_CONS = 1U;
  config.CNT_ARR_1016 = 1U;
  config.CNT_EM = 1U;
  config.CNT_SDO_SRV = 0U;
  config.CNT_SDO_CLI = 1U;
  config.CNT_TIME = 0U;
  config.CNT_SYNC = 0U;
  config.CNT_RPDO = 4U;
  config.CNT_TPDO = 0U;
  config.CNT_LEDS = 0U;
  config.CNT_GFC = 0U;
  config.CNT_SRDO = 0U;
  config.CNT_LSS_SLV = 0U;
  config.CNT_LSS_MST = 0U;
  config.CNT_GTWA = 0U;
  config.CNT_TRACE = 0U;
}

} // namespace

StackStorage::StackStorage(StackConfig config) noexcept
    : config_{std::move(config)} {}

StackStorage::CreateResult StackStorage::create(StackConfig config) noexcept {
  const auto status = validate_stack_config(config);
  if (!status.ok()) {
    return CreateResult::failure(status);
  }

  const auto context = identity_context(config);
  if (stack_claim.test_and_set(std::memory_order_acquire)) {
    return CreateResult::failure(platform::linux::Status::from_errno(
        "claim_canopen_stack", context, EBUSY));
  }

  auto owner = std::unique_ptr<StackStorage>{
      new (std::nothrow) StackStorage(std::move(config))};
  if (!owner) {
    stack_claim.clear(std::memory_order_release);
    return CreateResult::failure(platform::linux::Status::from_errno(
        "allocate_canopen_stack_owner", context, ENOMEM));
  }

  reset_generated_dictionary();
  apply_receive_configuration(owner->config_);
  configure_active_counts(owner->upstream_config_);
  owner->stack_ = CO_new(&owner->upstream_config_, &owner->heap_memory_used_);
  if (owner->stack_ == nullptr) {
    owner.reset();
    return CreateResult::failure(
        platform::linux::Status::from_errno("CO_new", context, ENOMEM));
  }
  return CreateResult::success(std::move(owner));
}

StackStorage::~StackStorage() {
  CO_delete(stack_);
  stack_claim.clear(std::memory_order_release);
}

CO_t *StackStorage::stack() noexcept { return stack_; }

OD_t *StackStorage::object_dictionary() noexcept { return OD; }

const CO_config_t &StackStorage::upstream_config() const noexcept {
  return upstream_config_;
}

std::uint32_t StackStorage::heap_memory_used() const noexcept {
  return heap_memory_used_;
}

const StackConfig &StackStorage::config() const noexcept { return config_; }

} // namespace robot_control::communication::canopen
