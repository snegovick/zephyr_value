/*
 * Copyright (c) 2023 MBT
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Defines for ADC values polling.
 */

#ifndef ZEPHYR_INCLUDE_DT_BINDINGS_VALUE_ADC_H_
#define ZEPHYR_INCLUDE_DT_BINDINGS_VALUE_ADC_H_

/**
 * @brief ADC Values Device-Tree constants
 * @defgroup adc_values_dt ADC Values Interface
 * @ingroup device_tree
 * @{
 */

#define ADC_VALUES_DT_COMPAT adc_values

/**
 * @brief Current state of poller
 */
#define ADC_VALUES_STATE 0

/**
 * @brief Poller synchronization
 */
#define ADC_VALUES_SYNC 1

/**
 * @brief Number of configured channels
 */
#define ADC_VALUES_NUM_CHANNELS 2

#define ADC_VALUES_CHANNEL_FLAG (1 << 16)
#define ADC_VALUES_CHANNEL_GET(id) ((id) &~ADC_VALUES_CHANNEL_FLAG)

/**
 * @brief Channel values
 */
#define ADC_VALUES_CHANNEL(n) ((n) | ADC_VALUES_CHANNEL_FLAG)

/**
 * @}
 */

#endif /* ZEPHYR_INCLUDE_DT_BINDINGS_VALUE_ADC_H_ */
