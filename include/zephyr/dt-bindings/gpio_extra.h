/*
 * Copyright (c) 2022 Elpitech
 *
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 *
 */

#ifndef ZEPHYR_INCLUDE_DT_BINDINGS_GPIO_EXTRA_H_
#define ZEPHYR_INCLUDE_DT_BINDINGS_GPIO_EXTRA_H_

/**
 * @brief GPIO Driver APIs
 * @defgroup gpio_interface GPIO Driver APIs
 * @ingroup io_interfaces
 * @{
 */

/**
 * @name GPIO pin edge detection flags
 * @{
 */

/** Trigger detection when input state is (or transitions to) logical 0 level. */
#define GPIO_EDGE_TO_INACTIVE (1 << 8)

/** Trigger detection on input state is (or transitions to) logical 1 level. */
#define GPIO_EDGE_TO_ACTIVE (1 << 9)

/**
 * @}
 */

/**
 * @}
 */

#endif /* ZEPHYR_INCLUDE_DT_BINDINGS_GPIO_EXTRA_H_ */
