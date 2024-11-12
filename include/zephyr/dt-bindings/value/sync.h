/*
 * Copyright (c) 2023 MBT
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Defines for value synchronizer.
 */

#ifndef ZEPHYR_INCLUDE_DT_BINDINGS_VALUE_SYNC_H_
#define ZEPHYR_INCLUDE_DT_BINDINGS_VALUE_SYNC_H_

/**
 * @brief Value Synchronizer Device-Tree constants
 * @defgroup value_sync_dt Value Synchronizer Interface
 * @ingroup device_tree
 * @{
 */

#define SYNC_DT_COMPAT value_sync

/**
 * @brief Current state of synchronizer
 */
#define SYNC_STATE 0

/**
 * @brief Minimum cycles counted when timing
 */
#define SYNC_MIN_CYCLES 1

/**
 * @brief Maximum cycles counted when timing
 */
#define SYNC_MAX_CYCLES 2

/**
 * @}
 */

#endif /* ZEPHYR_INCLUDE_DT_BINDINGS_VALUE_SYNC_H_ */
