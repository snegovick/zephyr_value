/*
 * Copyright (c) 2023 MBT
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Value mixer parameters
 */

#ifndef ZEPHYR_INCLUDE_DT_BINDINGS_VALUE_MIX_H_
#define ZEPHYR_INCLUDE_DT_BINDINGS_VALUE_MIX_H_

/**
 * @brief Value mixer Device-Tree defines
 * @defgroup mixer_dt Value mixer Device-Tree defines
 * @ingroup device_tree
 * @{
 */

/**
 * @brief Device-Tree compatible indentifier
 */
#define MIX_DT_COMPAT value_mix

/**
 * @brief Current state of driver (0 - off, 1 - on)
 */
#define MIX_STATE (1 << 16)

/**
 * @brief Value sync identifier
 */
#define MIX_SYNC (2 << 16)

/**
 * @brief Output value identifier
 */
#define MIX_OUTPUT (3 << 16)

/**
 * @brief Number of input values identifier
 */
#define MIX_INPUTS (4 << 16)

/**
 * @brief Weight value identifiers
 *
 * @param n Value number
 */
#define MIX_WEIGHT(n) (n)

/**
 * @brief Identifier to invoke command
 *
 * To select command pass corresponding constant as a value.
 */
#define MIX_COMMAND (5 << 16)

/**
 * @brief Load previously saved weights from settings
 */
#define MIX_WEIGHTS_LOAD 1

/**
 * @brief Save current weight values in settings
 */
#define MIX_WEIGHTS_SAVE 2

/**
 * @brief Restore default weight values
 *
 * Also you may need invoke save to drop previously saved values from settings.
 */
#define MIX_WEIGHTS_RESET 3

/**
 * @}
 */

#endif /* ZEPHYR_INCLUDE_DT_BINDINGS_VALUE_MIX_H_ */
