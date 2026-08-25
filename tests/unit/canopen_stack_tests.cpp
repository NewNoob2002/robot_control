#include "communication/canopen/CO_driver_custom.h"
#include "communication/canopen/stack_storage.hpp"

#include <linux/if.h>

#include <array>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <string_view>

namespace {

using namespace std::chrono_literals;
using robot_control::communication::canopen::StackConfig;
using robot_control::communication::canopen::StackStorage;
using robot_control::communication::canopen::validate_stack_config;

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
  };
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
  }
  for (const auto timeout : {1ms, 65535ms}) {
    config = valid_config();
    config.heartbeat_timeout = timeout;
    config.sdo_timeout = timeout;
    CHECK("CANOPEN-CONFIG-015", validate_stack_config(config).ok());
  }
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

  auto second = StackStorage::create(valid_config("missing0", 2U));
  CHECK("CANOPEN-STACK-036", !second.ok());
  CHECK("CANOPEN-STACK-037", second.status().operation == "claim_canopen_stack");
  CHECK("CANOPEN-STACK-038", second.status().context == "controller=127 remote=2");
  CHECK("CANOPEN-STACK-039", second.status().error.value() == EBUSY);

  const auto heap_memory_used = first->heap_memory_used();
  first.reset();
  auto reacquired_result = StackStorage::create(valid_config("missing0", 2U));
  CHECK("CANOPEN-STACK-040", reacquired_result.ok());
  if (reacquired_result.ok()) {
    const auto &reacquired = reacquired_result.value();
    CHECK("CANOPEN-STACK-041",
          reacquired->heap_memory_used() == heap_memory_used);
    CHECK("CANOPEN-STACK-042",
          OD_PERSIST_COMM.x1016_consumerHeartbeatTime[0] == 0x000203e8U);
    CHECK("CANOPEN-STACK-043",
          OD_PERSIST_COMM.x1016_consumerHeartbeatTime[1] == 0U);
    CHECK("CANOPEN-STACK-044",
          OD_PERSIST_COMM.x1280_SDOClientParameter.node_IDOfTheSDOServer == 2U);
    CHECK("CANOPEN-STACK-045",
          OD_PERSIST_COMM.x1400_RPDOCommunicationParameter.COB_IDUsedByRPDO ==
              0x182U);
    CHECK("CANOPEN-STACK-046",
          OD_PERSIST_COMM.x1603_RPDOMappingParameter
                  .numberOfMappedApplicationObjectsInPDO == 0U);
  }
}

} // namespace

/** Run the host-only minimal CANopen allocation contract. */
int main() {
  test_validation();
  test_storage();
  if (failures != 0) {
    std::cerr << "canopen_stack_tests failures=" << failures << '\n';
    return EXIT_FAILURE;
  }
  std::cout << "canopen_stack_tests passed\n";
  return EXIT_SUCCESS;
}
