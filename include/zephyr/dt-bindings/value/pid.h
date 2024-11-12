/*
 * Copyright (c) 2023 MBT
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Unified PID-controller parameters
 */

#ifndef ZEPHYR_INCLUDE_DT_BINDINGS_VALUE_PID_H_
#define ZEPHYR_INCLUDE_DT_BINDINGS_VALUE_PID_H_

/**
 * @brief Unified PID Device-Tree defines
 * @defgroup pid_dt PID Device-Tree defines
 * @ingroup device_tree
 * @{
 */

/**
 * @brief Current state of controller (0 - off, 1 - on)
 */
#define PID_CONTROL_STATE 0

/**
 * @brief PID sync identifier
 */
#define PID_SYNC 20

/**
 * @brief Setpoint value identifier
 */
#define PID_SETPOINT_VALUE 1

/**
 * @brief Proportional gain value identifier
 */
#define PID_PROPORTIONAL_GAIN 2

/**
 * @brief Integral gain value identifier
 */
#define PID_INTEGRAL_GAIN 3

/**
 * @brief Derivative gain value identifier
 */
#define PID_DERIVATIVE_GAIN 4

/**
 * @brief Integral error limit value identifier
 *
 * Absolute value of integral error which must not be exceeded.
 */
#define PID_INTEGRAL_LIMIT 5

/**
 * @brief Proportional gain scale identifier (read-only)
 */
#define PID_PROPORTIONAL_SCALE 6

/**
 * @brief Integral gain scale identifier (read-only)
 */
#define PID_INTEGRAL_SCALE 7

/**
 * @brief Derivative gain scale identifier (read-only)
 */
#define PID_DERIVATIVE_SCALE 8

/**
 * @brief Feedback scale identifier (read-only)
 */
#define PID_FEEDBACK_SCALE 9

/**
 * @brief Setpoint scale identifier (read-only)
 */
#define PID_SETPOINT_SCALE 10

/**
 * @brief Control scale identifier (read-only)
 */
#define PID_CONTROL_SCALE 11

/**
 * @brief Internal scale identifier (read-only)
 */
#define PID_INTERNAL_SCALE 12

/**
 * @brief Control minimum value identifier (read-only)
 */
#define PID_CONTROL_MIN 13

/**
 * @brief Control maximum value identifier (read-only)
 */
#define PID_CONTROL_MAX 14

/**
 * @}
 */

#endif /* ZEPHYR_INCLUDE_DT_BINDINGS_VALUE_PID_H_ */
