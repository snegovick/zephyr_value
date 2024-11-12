/*
 * Copyright (c) 2023 MBT
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Direct controller
 */

#ifndef ZEPHYR_INCLUDE_DT_BINDINGS_VALUE_DCTL_H_
#define ZEPHYR_INCLUDE_DT_BINDINGS_VALUE_DCTL_H_

/**
 * @brief Direct controller Device-Tree defines
 * @defgroup direct_ctrl_dt Direct conttoller Device-Tree defines
 * @ingroup device_tree
 * @{
 */

/**
 * @brief Device-Tree compatible indentifier
 */
#define DCTL_DT_COMPAT direct_controller

/**
 * @brief Current state of driver (0 - off, 1 - on)
 */
#define DCTL_STATE 0

/**
 * @brief Value sync identifier
 */
#define DCTL_SYNC 1

/**
 * @brief Control value identifier
 */
#define DCTL_CONTROL 2

/**
 * @brief Number of points identifier
 */
#define DCTL_POINTS 3

/**
 * @brief Maximum number of points identifier
 */
#define DCTL_MAX_POINTS 4

#define _DCTL_POINT_FLAG (1 << 10)
#define _DCTL_POINT_CONTROL_FLAG (1 << 9)

#define DCTL_IS_POINT(id) ((id) &_DCTL_POINT_FLAG)
#define DCTL_IS_CONTROL(id) ((id) &_DCTL_POINT_CONTROL_FLAG)
#define DCTL_IS_FEEDBACK(id) (!DCTL_IS_CONTROL(id))

#define DCTL_POINT_IDX(id) ((id) &~(_DCTL_POINT_FLAG | _DCTL_POINT_CONTROL_FLAG))

/**
 * @brief Curve point feedback property identifier
 */
#define DCTL_POINT_FEEDBACK(idx) ((idx) | _DCTL_POINT_FLAG)

/**
 * @brief Curve point control property identifier
 */
#define DCTL_POINT_CONTROL(idx) ((idx) | (_DCTL_POINT_FLAG | _DCTL_POINT_CONTROL_FLAG))

/**
 * @brief Identifier to invoke command
 *
 * To select command pass corresponding constant as a value.
 */
#define DCTL_COMMAND 5

/**
 * @brief Load previously saved control points from settings
 */
#define DCTL_POINTS_LOAD 1

/**
 * @brief Save current values of control points in settings
 */
#define DCTL_POINTS_SAVE 2

/**
 * @brief Restore default control point values
 *
 * Also you may need invoke save to drop previously saved values from settings.
 */
#define DCTL_POINTS_RESET 3

/**
 * @}
 */

#endif /* ZEPHYR_INCLUDE_DT_BINDINGS_VALUE_DCTL_H_ */
