/*
 * Copyright (c) 2023 MBT
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Value min/max parameters
 */

#ifndef ZEPHYR_INCLUDE_DT_BINDINGS_VALUE_MINMAX_H_
#define ZEPHYR_INCLUDE_DT_BINDINGS_VALUE_MINMAX_H_

/**
 * @brief Value min/max Device-Tree defines
 * @defgroup minmax_dt Value min/max Device-Tree defines
 * @ingroup device_tree
 * @{
 */

/**
 * @brief Device-Tree compatible indentifier
 */
#define MINMAX_DT_COMPAT value_minmax

/**
 * @brief Current state of driver (0 - off, 1 - on)
 */
#define MINMAX_STATE 0

/**
 * @brief Value sync identifier
 */
#define MINMAX_SYNC 1

#define MINMAX_CH_ID_FIRST 2
#define MINMAX_CH_ID_COUNT 2

#define MINMAX_CH_TYPE_MIN 0
#define MINMAX_CH_TYPE_MAX 1

#define MINMAX_CH_IDX(id) \
	(((id) -MINMAX_CH_ID_FIRST) / MINMAX_CH_ID_COUNT)
#define MINMAX_CH_TYPE(id) \
	(((id) -MINMAX_CH_ID_FIRST) % MINMAX_CH_ID_COUNT)

/**
 * @brief Minimum value identifiers
 *
 * @param n Value number
 */
#define MINMAX_MINIMUM(n)			   \
	(MINMAX_CH_ID_FIRST + MINMAX_CH_TYPE_MIN + \
	 (n) * MINMAX_CH_ID_COUNT)

/**
 * @brief Maximum value identifiers
 *
 * @param n Value number
 */
#define MINMAX_MAXIMUM(n)			   \
	(MINMAX_CH_ID_FIRST + MINMAX_CH_TYPE_MAX + \
	 (n) * MINMAX_CH_ID_COUNT)

/**
 * @}
 */

#endif /* ZEPHYR_INCLUDE_DT_BINDINGS_VALUE_MINMAX_H_ */
