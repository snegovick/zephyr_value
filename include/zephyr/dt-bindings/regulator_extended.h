/**
 * @file
 * @brief API for voltage and current regulators.
 */

#ifndef ZEPHYR_INCLUDE_DT_BINDINGS_REGULATOR_EXTENDED_H_
#define ZEPHYR_INCLUDE_DT_BINDINGS_REGULATOR_EXTENDED_H_

/**
 * @brief Extended Regulator Device-Tree constants
 * @defgroup regulator_extended_dt Regulator Interface
 * @ingroup device_tree
 * @{
 */

#define REGEXT_DT_COMPAT regulator_extended

/**
 * @brief State of regulator
 *
 * Regulator can be turned on or off by setting this value.
 * Set @p true to turn it on, set @p false to turn it off.
 *
 * Current status of regulator can be given by getting this value.
 *
 * If regulator is turning on or off then @p value_get will return -EAGAIN.
 * If regulator failed to turn its state then @p value_get will return -ECANCELED.
 */
#define REGEXT_STATE 0

/**
 * @brief Current status of regulator power good
 *
 * @return true when power good signals stands in expected state (i.e. all is
 * active for enabled regulator or all is inactive for disabled one)
 */
#define REGEXT_PGOOD 1

/**
 * @}
 */

#endif /* ZEPHYR_INCLUDE_DT_BINDINGS_REGULATOR_EXTENDED_H_ */
