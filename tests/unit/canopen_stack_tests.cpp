#include "communication/canopen/CO_driver_custom.h"
#include "communication/canopen/lifecycle.hpp"
#include "communication/canopen/observation.hpp"
#include "communication/canopen/stack_storage.hpp"
#include "platform/linux/process/termination_event.hpp"
#include "platform/linux/unique_fd.hpp"

#include <dirent.h>
#include <fcntl.h>
#include <linux/can.h>
#include <linux/if.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <initializer_list>
#include <limits>
#include <memory>
#include <string>
#include <string_view>

namespace {

using namespace std::chrono_literals;
using robot_control::communication::canopen::Lifecycle;
using robot_control::communication::canopen::ObservationGeneration;
using robot_control::communication::canopen::ObservationStore;
using robot_control::communication::canopen::RawCanopenFrame;
using robot_control::communication::canopen::RemoteNmtState;
using robot_control::communication::canopen::StackConfig;
using robot_control::communication::canopen::StackStorage;
using robot_control::communication::canopen::validate_stack_config;
using robot_control::platform::linux::process::TerminationEvent;
using robot_control::platform::linux::UniqueFd;

static_assert(CO_CONFIG_NMT ==
              (CO_CONFIG_NMT_CALLBACK_CHANGE |
               CO_CONFIG_GLOBAL_FLAG_TIMERNEXT));
static_assert(CO_CONFIG_HB_CONS ==
              (CO_CONFIG_HB_CONS_ENABLE | CO_CONFIG_HB_CONS_CALLBACK_MULTI |
               CO_CONFIG_HB_CONS_QUERY_FUNCT |
               CO_CONFIG_GLOBAL_FLAG_TIMERNEXT));
static_assert(CO_CONFIG_NODE_GUARDING == 0);
static_assert(CO_CONFIG_EM ==
              (CO_CONFIG_EM_CONSUMER | CO_CONFIG_GLOBAL_FLAG_TIMERNEXT));
static_assert(CO_CONFIG_SDO_SRV == 0);
static_assert(CO_CONFIG_SDO_CLI ==
              (CO_CONFIG_SDO_CLI_ENABLE |
               CO_CONFIG_GLOBAL_FLAG_TIMERNEXT));
static_assert(CO_CONFIG_SDO_CLI_BUFFER_SIZE == 32U);
static_assert(CO_CONFIG_TIME == 0);
static_assert(CO_CONFIG_SYNC == 0);
static_assert(CO_CONFIG_PDO ==
              (CO_CONFIG_RPDO_ENABLE | CO_CONFIG_GLOBAL_FLAG_TIMERNEXT));
static_assert(CO_CONFIG_LEDS == 0);
static_assert(CO_CONFIG_LSS == 0);
static_assert(CO_CONFIG_GFC == 0);
static_assert(CO_CONFIG_SRDO == 0);
static_assert(CO_CONFIG_GTW == 0);
static_assert(CO_CONFIG_CRC16 == 0);
static_assert(CO_CONFIG_FIFO == CO_CONFIG_FIFO_ENABLE);
static_assert(CO_CONFIG_STORAGE == 0);
static_assert(CO_CONFIG_TRACE == 0);
static_assert(CO_CONFIG_DEBUG == 0);
static_assert(CO_DRIVER_ERROR_REPORTING == 0);
static_assert(CO_DRIVER_MULTI_INTERFACE == 0);

int failures = 0;

/** Record one minimal-stack contract assertion failure. */
void check(const bool condition, const std::string_view id,
           const std::string_view expression) {
  if (!condition) {
    ++failures;
    std::cerr << id << " failed: " << expression << '\n';
  }
}

#define CHECK(id, expression) check((expression), (id), #expression)

/** Return an explicit valid sample; no value is a deployment default. */
StackConfig valid_config(std::string interface_name = "can0",
                         const std::uint8_t remote_node_id = 1U) {
  return StackConfig{
      .interface_name = std::move(interface_name),
      .controller_node_id = 127U,
      .remote_node_id = remote_node_id,
      .bit_rate_kbit_s = 500U,
      .heartbeat_timeout = 1000ms,
      .sdo_timeout = 500ms,
      .tpdo_timeout = 100ms,
      .tpdo_expected_dlc = {8U, 0U, 0U, 0U},
  };
}

/** Build one raw observation frame with deterministic unused payload bytes. */
RawCanopenFrame raw_frame(
    const std::uint32_t identifier, const std::uint8_t dlc,
    const std::initializer_list<std::uint8_t> payload,
    const std::chrono::steady_clock::time_point received_at,
    const ObservationGeneration generation) {
  RawCanopenFrame frame{
      .identifier = identifier,
      .dlc = dlc,
      .received_at = received_at,
      .generation = generation,
  };
  std::copy_n(payload.begin(),
              std::min(payload.size(), frame.payload.size()),
              frame.payload.begin());
  return frame;
}

/** Return whether every generated OD entry has no installed extension. */
bool all_extensions_clear() {
  for (std::uint16_t index = 0U; index < OD->size; ++index) {
    if (OD->list[index].extension != nullptr) {
      return false;
    }
  }
  return true;
}

/** Count process descriptors while including this helper's directory handle. */
std::size_t descriptor_count() {
  DIR *const directory = ::opendir("/proc/self/fd");
  if (directory == nullptr) {
    return std::numeric_limits<std::size_t>::max();
  }
  std::size_t count = 0U;
  while (::readdir(directory) != nullptr) {
    ++count;
  }
  static_cast<void>(::closedir(directory));
  return count;
}

/** Verify pure startup configuration validation at every specified boundary. */
void test_validation() {
  CHECK("CANOPEN-CONFIG-001", validate_stack_config(valid_config()).ok());
  CHECK("CANOPEN-CONFIG-002", !validate_stack_config(StackConfig{}).ok());

  auto config = valid_config();
  config.controller_node_id = 0U;
  CHECK("CANOPEN-CONFIG-003", !validate_stack_config(config).ok());
  config = valid_config();
  config.controller_node_id = 128U;
  CHECK("CANOPEN-CONFIG-004", !validate_stack_config(config).ok());
  config = valid_config();
  config.remote_node_id = 0U;
  CHECK("CANOPEN-CONFIG-005", !validate_stack_config(config).ok());
  config = valid_config();
  config.remote_node_id = 128U;
  CHECK("CANOPEN-CONFIG-006", !validate_stack_config(config).ok());
  config = valid_config();
  config.remote_node_id = config.controller_node_id;
  CHECK("CANOPEN-CONFIG-007", !validate_stack_config(config).ok());

  for (const std::string_view invalid : {"", ".", "..", "can/0", "can 0",
                                         "can\t0", "can\n0"}) {
    config = valid_config(std::string{invalid});
    CHECK("CANOPEN-CONFIG-008", !validate_stack_config(config).ok());
  }
  config = valid_config(std::string(IFNAMSIZ - 1U, 'x'));
  CHECK("CANOPEN-CONFIG-009", validate_stack_config(config).ok());
  config = valid_config(std::string(IFNAMSIZ, 'x'));
  CHECK("CANOPEN-CONFIG-010", !validate_stack_config(config).ok());

  for (const std::uint16_t bit_rate :
       std::array<std::uint16_t, 5>{100U, 125U, 250U, 500U, 1000U}) {
    config = valid_config();
    config.bit_rate_kbit_s = bit_rate;
    CHECK("CANOPEN-CONFIG-011", validate_stack_config(config).ok());
  }
  for (const std::uint16_t bit_rate :
       std::array<std::uint16_t, 4>{0U, 50U, 999U, 1001U}) {
    config = valid_config();
    config.bit_rate_kbit_s = bit_rate;
    CHECK("CANOPEN-CONFIG-012", !validate_stack_config(config).ok());
  }

  for (const auto timeout : {0ms, -1ms, 65536ms}) {
    config = valid_config();
    config.heartbeat_timeout = timeout;
    CHECK("CANOPEN-CONFIG-013", !validate_stack_config(config).ok());
    config = valid_config();
    config.sdo_timeout = timeout;
    CHECK("CANOPEN-CONFIG-014", !validate_stack_config(config).ok());
    config = valid_config();
    config.tpdo_timeout = timeout;
    CHECK("CANOPEN-CONFIG-016", !validate_stack_config(config).ok());
  }
  for (const auto timeout : {1ms, 65535ms}) {
    config = valid_config();
    config.heartbeat_timeout = timeout;
    config.sdo_timeout = timeout;
    config.tpdo_timeout = timeout;
    CHECK("CANOPEN-CONFIG-015", validate_stack_config(config).ok());
  }
  config = valid_config();
  config.tpdo_expected_dlc[2] = 9U;
  CHECK("CANOPEN-CONFIG-017", !validate_stack_config(config).ok());
}

/** Verify boot, freshness, malformed, replay, error, and reset contracts. */
void test_observation_contract() {
  const auto start = std::chrono::steady_clock::time_point{10s};
  ObservationStore observations{valid_config()};
  auto snapshot = observations.snapshot(start);
  CHECK("CANOPEN-OBS-001", snapshot.version == 0U);
  CHECK("CANOPEN-OBS-002", snapshot.generation == ObservationGeneration{});
  CHECK("CANOPEN-OBS-003", !snapshot.boot_observed);

  observations.begin_transport();
  const auto first_generation = observations.generation();
  CHECK("CANOPEN-OBS-004", first_generation.transport == 1U);
  CHECK("CANOPEN-OBS-005", first_generation.boot == 0U);

  observations.ingest(
      raw_frame(0x701U, 1U, {0x05U}, start, first_generation), start);
  snapshot = observations.snapshot(start);
  CHECK("CANOPEN-OBS-006", !snapshot.heartbeat.frame.current);
  CHECK("CANOPEN-OBS-007", !snapshot.nmt.current);

  observations.ingest(
      raw_frame(0x701U, 1U, {0x00U}, start + 1ms, first_generation),
      start + 1ms);
  const auto boot_generation = observations.generation();
  snapshot = observations.snapshot(start + 1ms);
  CHECK("CANOPEN-OBS-008", boot_generation.transport == 1U);
  CHECK("CANOPEN-OBS-009", boot_generation.boot == 1U);
  CHECK("CANOPEN-OBS-010", snapshot.boot_observed);
  CHECK("CANOPEN-OBS-011", snapshot.boot.current);
  CHECK("CANOPEN-OBS-012", snapshot.nmt.current);
  CHECK("CANOPEN-OBS-013",
        snapshot.nmt.state == RemoteNmtState::initializing);

  observations.ingest(
      raw_frame(0x701U, 1U, {0x05U}, start + 2ms, boot_generation),
      start + 2ms);
  observations.ingest(raw_frame(0x181U, 8U,
                                {0x00U, 0x14U, 0x00U, 0x14U, 0U, 0U, 0U, 0U},
                                start + 3ms, boot_generation),
                      start + 3ms);
  snapshot = observations.snapshot(start + 3ms);
  CHECK("CANOPEN-OBS-014", snapshot.heartbeat.frame.current);
  CHECK("CANOPEN-OBS-015",
        snapshot.nmt.state == RemoteNmtState::operational);
  CHECK("CANOPEN-OBS-016", snapshot.tpdo[0].current);
  CHECK("CANOPEN-OBS-017", snapshot.tpdo[0].raw.payload[1] == 0x14U);
  CHECK("CANOPEN-OBS-018", !snapshot.tpdo[1].current);
  observations.ingest(
      raw_frame(0x281U, 0U, {}, start + 3ms, boot_generation),
      start + 3ms);
  snapshot = observations.snapshot(start + 3ms);
  CHECK("CANOPEN-OBS-050", snapshot.tpdo[1].current);

  observations.ingest(raw_frame(0x281U, 1U, {0xAAU}, start + 4ms,
                                boot_generation),
                      start + 4ms);
  snapshot = observations.snapshot(start + 4ms);
  CHECK("CANOPEN-OBS-024", snapshot.malformed_count == 1U);
  CHECK("CANOPEN-OBS-025", snapshot.malformed.present);
  CHECK("CANOPEN-OBS-026", !snapshot.tpdo[1].current);
  CHECK("CANOPEN-OBS-027", snapshot.tpdo[0].current);

  observations.ingest(raw_frame(0x081U, 8U,
                                {0x10U, 0x23U, 0x04U, 0x07U, 0x78U, 0x56U,
                                 0x34U, 0x12U},
                                start + 5ms, boot_generation),
                      start + 5ms);
  snapshot = observations.snapshot(start + 5ms);
  CHECK("CANOPEN-OBS-028", snapshot.emergency.frame.current);
  CHECK("CANOPEN-OBS-029", snapshot.emergency.error_code == 0x2310U);
  CHECK("CANOPEN-OBS-030", snapshot.emergency.error_register == 0x04U);
  CHECK("CANOPEN-OBS-031", snapshot.emergency.error_bit == 0x07U);
  CHECK("CANOPEN-OBS-032", snapshot.emergency.info_code == 0x12345678U);

  observations.ingest(
      raw_frame(0x081U, 3U, {1U, 2U, 3U}, start + 6ms, boot_generation),
      start + 6ms);
  snapshot = observations.snapshot(start + 6ms);
  CHECK("CANOPEN-OBS-033", !snapshot.emergency.frame.current);
  CHECK("CANOPEN-OBS-034", snapshot.malformed_count == 2U);

  observations.ingest(raw_frame(0x701U, 1U, {0x04U}, start + 20ms,
                                boot_generation),
                      start + 10ms);
  snapshot = observations.snapshot(start + 10ms);
  CHECK("CANOPEN-OBS-035", snapshot.future_timestamp_count == 1U);
  CHECK("CANOPEN-OBS-036", !snapshot.heartbeat.frame.current);

  const auto before_replay_version = snapshot.version;
  observations.ingest(
      raw_frame(0x181U, 8U, {}, start + 11ms, first_generation),
      start + 11ms);
  snapshot = observations.snapshot(start + 11ms);
  CHECK("CANOPEN-OBS-037", snapshot.replay_count == 1U);
  CHECK("CANOPEN-OBS-038", snapshot.version > before_replay_version);

  observations.ingest(
      raw_frame(CAN_ERR_FLAG | 0x04U, 8U, {0U}, start + 12ms,
                boot_generation),
      start + 12ms);
  const auto error_generation = observations.generation();
  snapshot = observations.snapshot(start + 12ms);
  CHECK("CANOPEN-OBS-039", error_generation.transport == 2U);
  CHECK("CANOPEN-OBS-040", error_generation.boot == 0U);
  CHECK("CANOPEN-OBS-041", snapshot.can_error.present);
  CHECK("CANOPEN-OBS-042", !snapshot.boot_observed);
  CHECK("CANOPEN-OBS-043", !snapshot.tpdo[0].current);

  observations.ingest(
      raw_frame(0x701U, 1U, {0x05U}, start + 13ms, error_generation),
      start + 13ms);
  snapshot = observations.snapshot(start + 13ms);
  CHECK("CANOPEN-OBS-044", !snapshot.heartbeat.frame.current);

  observations.begin_transport();
  const auto reopened_generation = observations.generation();
  snapshot = observations.snapshot(start + 14ms);
  CHECK("CANOPEN-OBS-045", reopened_generation.transport == 3U);
  CHECK("CANOPEN-OBS-046", !snapshot.boot_observed);
  CHECK("CANOPEN-OBS-047", !snapshot.sdo_result.current);
  observations.ingest(
      raw_frame(0x181U, 8U, {}, start + 14ms, boot_generation),
      start + 14ms);
  snapshot = observations.snapshot(start + 14ms);
  CHECK("CANOPEN-OBS-053", snapshot.replay_count == 2U);
  CHECK("CANOPEN-OBS-054", snapshot.generation == reopened_generation);
  CHECK("CANOPEN-OBS-055", !snapshot.tpdo[0].current);

  const auto immutable_copy = snapshot;
  observations.ingest(
      raw_frame(0x701U, 1U, {0x00U}, start + 15ms, reopened_generation),
      start + 15ms);
  CHECK("CANOPEN-OBS-048", immutable_copy.generation == reopened_generation);
  CHECK("CANOPEN-OBS-049", !immutable_copy.boot_observed);
}

/** Verify inclusive monotonic deadlines create one versioned invalidation. */
void test_observation_freshness() {
  const auto start = std::chrono::steady_clock::time_point{20s};
  ObservationStore observations{valid_config()};
  observations.begin_transport();
  const auto initial_generation = observations.generation();
  observations.ingest(
      raw_frame(0x701U, 1U, {0x00U}, start, initial_generation), start);
  const auto generation = observations.generation();
  observations.ingest(
      raw_frame(0x701U, 1U, {0x05U}, start + 1ms, generation),
      start + 1ms);
  observations.ingest(
      raw_frame(0x181U, 8U, {}, start + 2ms, generation), start + 2ms);

  auto snapshot = observations.snapshot(start + 102ms);
  const auto before_tpdo_timeout = snapshot.version;
  observations.advance_time(start + 102ms);
  snapshot = observations.snapshot(start + 102ms);
  CHECK("CANOPEN-OBS-019", snapshot.tpdo[0].current);
  CHECK("CANOPEN-OBS-020", snapshot.version == before_tpdo_timeout);

  observations.advance_time(start + 102ms + 1ns);
  snapshot = observations.snapshot(start + 102ms + 1ns);
  CHECK("CANOPEN-OBS-021", !snapshot.tpdo[0].current);
  CHECK("CANOPEN-OBS-022", snapshot.heartbeat.frame.current);
  CHECK("CANOPEN-OBS-023", snapshot.version > before_tpdo_timeout);

  const auto before_heartbeat_timeout = snapshot.version;
  observations.advance_time(start + 1001ms);
  snapshot = observations.snapshot(start + 1001ms);
  CHECK("CANOPEN-OBS-051", snapshot.heartbeat.frame.current);
  CHECK("CANOPEN-OBS-052", snapshot.version == before_heartbeat_timeout);

  observations.advance_time(start + 1001ms + 1ns);
  snapshot = observations.snapshot(start + 1001ms + 1ns);
  CHECK("CANOPEN-OBS-056", !snapshot.heartbeat.frame.current);
  CHECK("CANOPEN-OBS-057", !snapshot.nmt.current);
  CHECK("CANOPEN-OBS-058", snapshot.version > before_heartbeat_timeout);
}

/** Verify allocation, exact active counts, OD values, and no CAN activation. */
void test_storage() {
  auto first_result = StackStorage::create(valid_config("missing0"));
  CHECK("CANOPEN-STACK-001", first_result.ok());
  if (!first_result.ok()) {
    return;
  }
  auto first = std::move(first_result).value();
  CHECK("CANOPEN-STACK-002", first->stack() != nullptr);
  CHECK("CANOPEN-STACK-003", first->object_dictionary() != nullptr);
  CHECK("CANOPEN-STACK-004", first->heap_memory_used() > 0U);

  const auto &counts = first->upstream_config();
  CHECK("CANOPEN-STACK-005", counts.CNT_NMT == 1U);
  CHECK("CANOPEN-STACK-006", counts.CNT_HB_CONS == 1U);
  CHECK("CANOPEN-STACK-007", counts.CNT_ARR_1016 == 1U);
  CHECK("CANOPEN-STACK-008", counts.CNT_EM == 1U);
  CHECK("CANOPEN-STACK-009", counts.CNT_SDO_SRV == 0U);
  CHECK("CANOPEN-STACK-010", counts.CNT_SDO_CLI == 1U);
  CHECK("CANOPEN-STACK-011", counts.CNT_TIME == 0U);
  CHECK("CANOPEN-STACK-012", counts.CNT_SYNC == 0U);
  CHECK("CANOPEN-STACK-013", counts.CNT_RPDO == 4U);
  CHECK("CANOPEN-STACK-014", counts.CNT_TPDO == 0U);
  CHECK("CANOPEN-STACK-015", counts.CNT_LEDS == 0U);
  CHECK("CANOPEN-STACK-016", counts.CNT_GFC == 0U);
  CHECK("CANOPEN-STACK-017", counts.CNT_SRDO == 0U);
  CHECK("CANOPEN-STACK-018", counts.CNT_LSS_SLV == 0U);
  CHECK("CANOPEN-STACK-019", counts.CNT_LSS_MST == 0U);
  CHECK("CANOPEN-STACK-020", counts.CNT_GTWA == 0U);
  CHECK("CANOPEN-STACK-021", counts.CNT_TRACE == 0U);

  CHECK("CANOPEN-STACK-022", OD_PERSIST_COMM.x1017_producerHeartbeatTime == 0U);
  CHECK("CANOPEN-STACK-023",
        OD_PERSIST_COMM.x1016_consumerHeartbeatTime_sub0 == 1U);
  CHECK("CANOPEN-STACK-024",
        OD_PERSIST_COMM.x1016_consumerHeartbeatTime[0] == 0x000103e8U);
  CHECK("CANOPEN-STACK-025",
        OD_PERSIST_COMM.x1280_SDOClientParameter.COB_IDClientToServerTx ==
            0x601U);
  CHECK("CANOPEN-STACK-026",
        OD_PERSIST_COMM.x1280_SDOClientParameter.COB_IDServerToClientRx ==
            0x581U);
  CHECK("CANOPEN-STACK-027",
        OD_PERSIST_COMM.x1280_SDOClientParameter.node_IDOfTheSDOServer == 1U);
  CHECK("CANOPEN-STACK-028",
        OD_PERSIST_COMM.x1400_RPDOCommunicationParameter.COB_IDUsedByRPDO ==
            0x181U);
  CHECK("CANOPEN-STACK-029",
        OD_PERSIST_COMM.x1401_RPDOCommunicationParameter.COB_IDUsedByRPDO ==
            0x281U);
  CHECK("CANOPEN-STACK-030",
        OD_PERSIST_COMM.x1402_RPDOCommunicationParameter.COB_IDUsedByRPDO ==
            0x381U);
  CHECK("CANOPEN-STACK-031",
        OD_PERSIST_COMM.x1403_RPDOCommunicationParameter.COB_IDUsedByRPDO ==
            0x481U);
  CHECK("CANOPEN-STACK-032",
        OD_PERSIST_COMM.x1600_RPDOMappingParameter
                .numberOfMappedApplicationObjectsInPDO == 0U);
  CHECK("CANOPEN-STACK-033",
        first->stack()->CANmodule->CANinterfaces == nullptr);
  CHECK("CANOPEN-STACK-034",
        first->stack()->CANmodule->CANinterfaceCount == 0U);
  CHECK("CANOPEN-STACK-035", !first->stack()->CANmodule->CANnormal);
  CHECK("CANOPEN-STACK-036", all_extensions_clear());

  auto second = StackStorage::create(valid_config("missing0", 2U));
  CHECK("CANOPEN-STACK-037", !second.ok());
  CHECK("CANOPEN-STACK-038", second.status().operation == "claim_canopen_stack");
  CHECK("CANOPEN-STACK-039", second.status().context == "controller=127 remote=2");
  CHECK("CANOPEN-STACK-040", second.status().error.value() == EBUSY);

  OD_extension_t first_extension{};
  OD_extension_t last_extension{};
  CHECK("CANOPEN-STACK-041",
        OD_extension_init(&OD->list[0], &first_extension) == ODR_OK);
  CHECK("CANOPEN-STACK-042",
        OD_extension_init(&OD->list[OD->size - 1U], &last_extension) == ODR_OK);

  const auto heap_memory_used = first->heap_memory_used();
  first.reset();
  CHECK("CANOPEN-STACK-043", all_extensions_clear());
  auto reacquired_result = StackStorage::create(valid_config("missing0", 2U));
  CHECK("CANOPEN-STACK-044", reacquired_result.ok());
  if (reacquired_result.ok()) {
    const auto &reacquired = reacquired_result.value();
    CHECK("CANOPEN-STACK-045", all_extensions_clear());
    CHECK("CANOPEN-STACK-046",
          reacquired->heap_memory_used() == heap_memory_used);
    CHECK("CANOPEN-STACK-047",
          OD_PERSIST_COMM.x1016_consumerHeartbeatTime[0] == 0x000203e8U);
    CHECK("CANOPEN-STACK-048",
          OD_PERSIST_COMM.x1016_consumerHeartbeatTime[1] == 0U);
    CHECK("CANOPEN-STACK-049",
          OD_PERSIST_COMM.x1280_SDOClientParameter.node_IDOfTheSDOServer == 2U);
    CHECK("CANOPEN-STACK-050",
          OD_PERSIST_COMM.x1400_RPDOCommunicationParameter.COB_IDUsedByRPDO ==
              0x182U);
    CHECK("CANOPEN-STACK-051",
          OD_PERSIST_COMM.x1603_RPDOMappingParameter
                  .numberOfMappedApplicationObjectsInPDO == 0U);
    OD_extension_t reopen_extension{};
    CHECK("CANOPEN-STACK-052",
          OD_extension_init(&OD->list[0], &reopen_extension) == ODR_OK);
    reacquired->prepare_communication_reset();
    CHECK("CANOPEN-STACK-053", all_extensions_clear());
    CHECK("CANOPEN-STACK-054",
          reacquired->stack()->CANmodule->CANinterfaces == nullptr);
    CHECK("CANOPEN-STACK-055",
          OD_PERSIST_COMM.x1016_consumerHeartbeatTime[0] == 0x000203e8U);
  }
}

/** Prove the pinned NMT boot-up path cannot reach the Linux send syscall. */
void test_transmit_gate() {
  UniqueFd sink{::open("/dev/null", O_WRONLY | O_CLOEXEC)};
  CHECK("CANOPEN-TX-001", sink.get() >= 0);
  if (!sink) {
    return;
  }

  CO_CANinterface_t interface{};
  interface.fd = sink.get();
  CO_CANmodule_t module{};
  module.CANinterfaces = &interface;
  module.CANinterfaceCount = 1U;
  CO_CANtx_t heartbeat{};
  errno = 0;
  CHECK("CANOPEN-TX-002",
        CO_CANsend(&module, &heartbeat) == CO_ERROR_SYSCALL);
  CHECK("CANOPEN-TX-003", errno == EACCES);

  CO_EM_t emergency{};
  CO_NMT_t nmt{};
  nmt.operatingState = CO_NMT_INITIALIZING;
  nmt.operatingStatePrev = CO_NMT_INITIALIZING;
  nmt.em = &emergency;
  nmt.HB_CANdevTx = &module;
  nmt.HB_TXbuff = &heartbeat;

  CO_NMT_internalState_t state = CO_NMT_INITIALIZING;
  errno = 0;
  CHECK("CANOPEN-TX-004",
        CO_NMT_process(&nmt, &state, 0U, nullptr) == CO_RESET_NOT);
  CHECK("CANOPEN-TX-005", state == CO_NMT_PRE_OPERATIONAL);
  CHECK("CANOPEN-TX-006", errno == EACCES);
}

/** Verify missing-interface startup is contextual and releases every owner/fd. */
void test_lifecycle_startup_failure() {
  auto termination_result = TerminationEvent::create();
  CHECK("CANOPEN-LIFECYCLE-001", termination_result.ok());
  if (!termination_result.ok()) {
    return;
  }
  auto termination = std::move(termination_result).value();
  const auto descriptors_before = descriptor_count();
  CHECK("CANOPEN-LIFECYCLE-002",
        descriptors_before != std::numeric_limits<std::size_t>::max());

  auto created =
      Lifecycle::create(valid_config("p5_3_missing0"), termination);
  CHECK("CANOPEN-LIFECYCLE-003", !created.ok());
  CHECK("CANOPEN-LIFECYCLE-004",
        created.status().operation == "if_nametoindex");
  CHECK("CANOPEN-LIFECYCLE-005",
        created.status().context.find("interface=p5_3_missing0") !=
            std::string::npos);
  CHECK("CANOPEN-LIFECYCLE-006",
        created.status().context.find("controller=127") != std::string::npos);
  CHECK("CANOPEN-LIFECYCLE-007",
        created.status().context.find("remote=1") != std::string::npos);
  CHECK("CANOPEN-LIFECYCLE-008",
        created.status().context.find("upstream=not-called") !=
            std::string::npos);
  CHECK("CANOPEN-LIFECYCLE-009",
        created.status().error.value() == ENODEV);
  CHECK("CANOPEN-LIFECYCLE-010",
        all_extensions_clear());
  CHECK("CANOPEN-LIFECYCLE-011",
        descriptor_count() == descriptors_before);

  auto reacquired = StackStorage::create(valid_config("p5_3_missing0", 2U));
  CHECK("CANOPEN-LIFECYCLE-012", reacquired.ok());
  CHECK("CANOPEN-LIFECYCLE-013", all_extensions_clear());
}

} // namespace

/** Run the host-only minimal CANopen allocation contract. */
int main() {
  test_validation();
  test_observation_contract();
  test_observation_freshness();
  test_storage();
  test_transmit_gate();
  test_lifecycle_startup_failure();
  if (failures != 0) {
    std::cerr << "canopen_stack_tests failures=" << failures << '\n';
    return EXIT_FAILURE;
  }
  std::cout << "canopen_stack_tests passed\n";
  return EXIT_SUCCESS;
}
