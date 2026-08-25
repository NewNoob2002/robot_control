#pragma once

#define CO_CONFIG_NMT                                                         \
  (CO_CONFIG_NMT_CALLBACK_CHANGE | CO_CONFIG_GLOBAL_FLAG_TIMERNEXT)
#define CO_CONFIG_HB_CONS                                                     \
  (CO_CONFIG_HB_CONS_ENABLE | CO_CONFIG_HB_CONS_CALLBACK_MULTI |             \
   CO_CONFIG_HB_CONS_QUERY_FUNCT | CO_CONFIG_GLOBAL_FLAG_TIMERNEXT)
#define CO_CONFIG_NODE_GUARDING 0
#define CO_CONFIG_EM                                                          \
  (CO_CONFIG_EM_CONSUMER | CO_CONFIG_GLOBAL_FLAG_TIMERNEXT)
#define CO_CONFIG_SDO_SRV 0
#define CO_CONFIG_SDO_CLI                                                     \
  (CO_CONFIG_SDO_CLI_ENABLE | CO_CONFIG_GLOBAL_FLAG_TIMERNEXT)
#define CO_CONFIG_SDO_CLI_BUFFER_SIZE 32U
#define CO_CONFIG_TIME 0
#define CO_CONFIG_SYNC 0
#define CO_CONFIG_PDO                                                         \
  (CO_CONFIG_RPDO_ENABLE | CO_CONFIG_GLOBAL_FLAG_TIMERNEXT)
#define CO_CONFIG_LEDS 0
#define CO_CONFIG_LSS 0
#define CO_CONFIG_GFC 0
#define CO_CONFIG_SRDO 0
#define CO_CONFIG_GTW 0
#define CO_CONFIG_CRC16 0
#define CO_CONFIG_FIFO CO_CONFIG_FIFO_ENABLE
#define CO_CONFIG_STORAGE 0
#define CO_CONFIG_TRACE 0
#define CO_CONFIG_DEBUG 0
#define CO_DRIVER_ERROR_REPORTING 0
#define CO_DRIVER_MULTI_INTERFACE 0
