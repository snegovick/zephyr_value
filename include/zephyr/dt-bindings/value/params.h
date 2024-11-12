/*
 * Copyright (c) 2023 MBT
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Value parameters
 */

#ifndef ZEPHYR_INCLUDE_DT_BINDINGS_VALUE_PARAMS_H_
#define ZEPHYR_INCLUDE_DT_BINDINGS_VALUE_PARAMS_H_

/**
 * @brief Value params Device-Tree defines
 * @defgroup value_params_dt Value parameters Device-Tree defines
 * @ingroup device_tree
 * @{
 */

/**
 * @brief Device-Tree compatible indentifier
 */
#define PARAMS_DT_COMPAT value_params

/**
 * @brief Number of all parameters identifier
 */
#define PARAMS_NUMBER_ALL (1 << 16)

/**
 * @brief Number of non-volatile parameters identifier
 */
#define PARAMS_NUMBER_NV (2 << 16)

/**
 * @brief Identifier to invoke command
 *
 * To select command pass corresponding constant as a value.
 */
#define PARAMS_COMMAND (3 << 16)

/**
 * @brief Load previously saved parameters from settings
 */
#define PARAMS_LOAD 1

/**
 * @brief Save current values of parameters in settings
 */
#define PARAMS_SAVE 2

/**
 * @brief Restore default values
 *
 * Also you may need invoke save to drop previously saved value from settings.
 */
#define PARAMS_RESET 3

/**
 * @}
 */

#endif /* ZEPHYR_INCLUDE_DT_BINDINGS_VALUE_PARAMS_H_ */
