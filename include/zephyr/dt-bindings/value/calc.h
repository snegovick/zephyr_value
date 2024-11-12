/*
 * Copyright (c) 2023 MBT
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Calculated values
 */

#ifndef ZEPHYR_INCLUDE_DT_BINDINGS_VALUE_CALC_H_
#define ZEPHYR_INCLUDE_DT_BINDINGS_VALUE_CALC_H_

/**
 * @brief Value calc Device-Tree defines
 * @defgroup value_calc_dt Calculated values Device-Tree defines
 * @ingroup device_tree
 * @{
 */

/**
 * @brief Device-Tree compatible indentifier
 */
#define CALC_DT_COMPAT value_calc

/**
 * @brief The identifier of state
 */
#define CALC_STATE (1 << 16)

/**
 * @brief The identifier of sync
 */
#define CALC_SYNC (2 << 16)

/**
 * @brief The identifier for number of results
 */
#define CALC_RESULTS (3 << 16)

/**
 * @brief The identifier for results
 */
#define CALC_RESULT(idx) (idx)

/**
 * @}
 */

#endif /* ZEPHYR_INCLUDE_DT_BINDINGS_VALUE_CALC_H_ */
