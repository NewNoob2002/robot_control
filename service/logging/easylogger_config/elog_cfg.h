/*
 * Project configuration derived from EasyLogger's demo/os/linux example.
 * Copyright (c) 2015 Armink; distributed under the EasyLogger MIT license.
 *
 * Production deltas from the example: file, color, buffered, and asynchronous
 * output are disabled. journald already provides persistence/transport, while
 * synchronous output makes failures observable through the project port.
 */
#ifndef _ELOG_CFG_H_
#define _ELOG_CFG_H_

#define ELOG_OUTPUT_ENABLE
#define ELOG_OUTPUT_LVL ELOG_LVL_VERBOSE
#define ELOG_ASSERT_ENABLE
#define ELOG_LINE_BUF_SIZE 512
#define ELOG_LINE_NUM_MAX_LEN 5
#define ELOG_FILTER_TAG_MAX_LEN 16
#define ELOG_FILTER_KW_MAX_LEN 16
#define ELOG_FILTER_TAG_LVL_MAX_NUM 5
#define ELOG_NEWLINE_SIGN "\n"

#endif /* _ELOG_CFG_H_ */
