#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/value.h>
#include <zephyr/dt-bindings/i2c_config_switch.h>
#include <zephyr/drivers/i2c_ext.h>

#define DT_DRV_COMPAT I2C_CONFIG_SWITCH_DT_COMPAT

#include <zephyr/logging/log_ctrl.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(i2c_config_switch, CONFIG_I2C_CONFIG_SWITCH_LOG_LEVEL);

struct driver_config {
	const struct device *i2c_bus;
};

struct driver_data {
	const struct device *dev;
	int state;
};

static int i2c_config_switch_get(const struct device *dev,
				 value_id_t id,
				 value_t *pvalue)
{
	struct driver_data *data = dev->data;

	LOG_DBG("Get switch");

	switch (id) {
	case I2C_CONFIG_SWITCH_ENABLE_TARGET:
		*pvalue = data->state;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int i2c_config_switch_set(const struct device *dev,
				 value_id_t id,
				 value_t value)
{
	const struct driver_config *cfg = dev->config;
	struct driver_data *data = dev->data;

	LOG_DBG("Set switch");

	switch (id) {
	case I2C_CONFIG_SWITCH_ENABLE_TARGET:
		if (value) {
			LOG_DBG("Register all targets");
			i2c_stm32_target_register_all(cfg->i2c_bus);
			data->state = 1;
		} else {
			LOG_DBG("Unregister all targets");
			i2c_stm32_target_unregister_all(cfg->i2c_bus);
			data->state = 0;
		}
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static const struct value_driver_api i2c_config_switch_api = {
	.set = i2c_config_switch_set,
	.get = i2c_config_switch_get,
};

static int i2c_config_switch_init(const struct device *dev)
{
	struct driver_data *data = dev->data;

	data->state = 0;
	return 0;
}

#define I2C_CONFIG_SWITCH_DEVICE(id)					    \
									    \
	static const struct driver_config i2c_config_switch_config_##id = { \
		.i2c_bus = DEVICE_DT_GET(DT_INST_PHANDLE(id, i2c_bus)),	    \
	};								    \
									    \
	static struct driver_data i2c_config_switch_data_##id = {	    \
		.dev = DEVICE_DT_GET(DT_DRV_INST(id)),			    \
	};								    \
									    \
	DEVICE_DT_INST_DEFINE(id, i2c_config_switch_init, NULL,		    \
			      &i2c_config_switch_data_##id,		    \
			      &i2c_config_switch_config_##id, POST_KERNEL,  \
			      CONFIG_I2C_CONFIG_SWITCH_INIT_PRIORITY,	    \
			      &i2c_config_switch_api);

DT_INST_FOREACH_STATUS_OKAY(I2C_CONFIG_SWITCH_DEVICE)
