/*
 * Copyright (c) 2023 MBT
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Unified value filter parameters
 */

#ifndef ZEPHYR_INCLUDE_DT_BINDINGS_VALUE_FILTER_H_
#define ZEPHYR_INCLUDE_DT_BINDINGS_VALUE_FILTER_H_

/**
 * @brief Unified FILTER Device-Tree defines
 * @defgroup filter_dt FILTER Device-Tree defines
 * @ingroup device_tree
 * @{
 */

/**
 * @brief Filter Device-Tree compatible indentifier
 */
#define FILTER_DT_COMPAT value_filter

/**
 * @brief Output value identifier
 */
#define FILTER_OUTPUT(ch) (ch)

/**
 * @brief Current state of filter (0 - off, 1 - on)
 */
#define FILTER_STATE (1 << 16)

/**
 * @brief Filter sync identifier
 */
#define FILTER_SYNC (2 << 16)

/**
 * @brief Alpha parameter value identifier
 */
#define FILTER_ALPHA (3 << 16)

/**
 * @brief Samples parameter value identifier
 */
#define FILTER_SAMPLES (4 << 16)

/**
 * @brief Time window parameter value identifier
 *
 * Time values measured in seconds.
 */
#define FILTER_WINDOW (5 << 16)

/**
 * @brief Time period value identifier (readonly)
 *
 * Time values measured in seconds.
 */
#define FILTER_PERIOD (6 << 16)

/**
 * @brief Number of filtering values identifier (readonly)
 */
#define FILTER_VALUES (7 << 16)

/**
 * @brief Identifier to invoke command
 *
 * To select command pass corresponding constant as a value.
 */
#define FILTER_COMMAND (8 << 16)

/**
 * @brief Load previously saved filter parameter from settings
 */
#define FILTER_PARAM_LOAD 1

/**
 * @brief Save current value of filter parameter in settings
 */
#define FILTER_PARAM_SAVE 2

/**
 * @brief Restore default value
 *
 * Also you may need invoke save to drop previously saved value from settings.
 */
#define FILTER_PARAM_RESET 3

/**
 * @}
 */

#endif /* ZEPHYR_INCLUDE_DT_BINDINGS_VALUE_FILTER_H_ */
