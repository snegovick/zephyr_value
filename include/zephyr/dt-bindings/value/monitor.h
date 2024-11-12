/*
 * Copyright (c) 2023 MBT
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Defines for condition monitor.
 */

#ifndef ZEPHYR_INCLUDE_DT_BINDINGS_VALUE_MONITOR_H_
#define ZEPHYR_INCLUDE_DT_BINDINGS_VALUE_MONITOR_H_

/**
 * @brief Condition Monitor Device-Tree constants
 * @defgroup condition_mon_dt Condition Monitor Interface
 * @ingroup device_tree
 * @{
 */

#define MONITOR_DT_COMPAT condition_monitor

/**
 * @brief Current state of monitor
 */
#define MONITOR_STATE 0

/**
 * @brief Monitor synchronization
 */
#define MONITOR_SYNC 1

/**
 * @}
 */

#endif /* ZEPHYR_INCLUDE_DT_BINDINGS_VALUE_MONITOR_H_ */
