#include <zephyr/dt-bindings/value/monitor.h>
#include <zephyr/drivers/value.h>
#include <zephyr/device.h>
#include <zephyr/logging/log.h>

#define DT_DRV_COMPAT MONITOR_DT_COMPAT

LOG_MODULE_REGISTER(monitor, CONFIG_CONDITION_MONITOR_LOG_LEVEL);

struct monitor_data {
	struct value_sub sub;

	bool active;
	bool fault;
};

struct monitor_config {
	/* monitoring values */
	const struct value_dt_spec *values;

	value_t minimum;
	value_t maximum;

	uint8_t num_values;
};

static void monitor_task(const struct device *dev)
{
	const struct monitor_config *config = dev->config;
	struct monitor_data *data = dev->data;

	const struct value_dt_spec *value_spec = config->values;
	const struct value_dt_spec *end_value_spec = config->values + config->num_values;

	value_t value;
	int rc;
	bool under;
	bool over;

	if (data->fault) {
		return;
	}

	for (; value_spec < end_value_spec; value_spec++) {
		rc = value_get_dt(value_spec, &value);

		if (rc != 0) {
			if (rc != -EAGAIN) {
				LOG_ERR("%s: Error when getting value", dev->name);
			}
			continue;
		}

		over = value > config->maximum;
		under = value < config->minimum;

		if (over || under) {
			LOG_WRN("%s: %svalue detected", dev->name,
				over ? "Over" : "Under");
			data->fault = true;
			value_sub_notify(&data->sub, dev, MONITOR_STATE);
			break;
		}
	}
}

static int monitor_value_get(const struct device *dev, value_id_t id, value_t *pval)
{
	struct monitor_data *data = dev->data;
	int rc = 0;

	switch (id) {
	case MONITOR_STATE:
		*pval = data->active;
		rc = data->fault ? -EFAULT : 0;
		break;

	default:
		LOG_ERR("%s: attempt to get unknown value #%d", dev->name, id);
		rc = -EINVAL;
	}

	return rc;
}

static int monitor_value_set(const struct device *dev, value_id_t id, value_t val)
{
	struct monitor_data *data = dev->data;
	int rc = 0;

	switch (id) {
	case MONITOR_STATE:
		data->active = val;
		data->fault = false;
		break;

	case MONITOR_SYNC:
		if (!data->active) {
			break;
		}

		monitor_task(dev);

		break;

	default:
		LOG_ERR("%s: attempt to set unknown value #%d", dev->name, id);
		rc = -EINVAL;
	}

	return rc;
}

int monitor_value_sub(const struct device *dev, value_id_t id, struct value_sub_cb *cb, bool on)
{
	struct monitor_data *data = dev->data;
	int rc = 0;

	switch (id) {
	case MONITOR_STATE:
		value_sub_manage(&data->sub, cb, on);
		break;

	default:
		LOG_ERR("%s: attempt to subscribe to unknown value #%d", dev->name, id);
		rc = -EINVAL;
	}

	return rc;
}

static const struct value_driver_api monitor_api = {
	.get = monitor_value_get,
	.set = monitor_value_set,
	.sub = monitor_value_sub,
};

static int monitor_init(const struct device *dev)
{
	return 0;
}

#define DT_NUM_CONS(numerator, denominator) ((double)(numerator) / (double)(denominator))

#define DT_INST_PROP_NUM_OR(inst, prop, default_value)				   \
	COND_CODE_1(DT_INST_PROP_HAS_IDX(inst, prop, 0),			   \
		    (DT_NUM_CONS(DT_INST_PROP_BY_IDX(inst, prop, 0),		   \
				 COND_CODE_1(DT_INST_PROP_HAS_IDX(inst, prop, 1),  \
					     (DT_INST_PROP_BY_IDX(inst, prop, 1)), \
					     (1)))),				   \
		    (default_value))

#define DT_INST_PROP_NUM_OR_SCALED(inst, prop, default_value, scale_prop) \
	(DT_INST_PROP_NUM_OR(inst, prop, default_value) *		  \
	 DT_INST_PROP_OR(inst, scale_prop, 1))

#define MONITOR_VALUE_DT_SPEC(node_id, prop, idx) \
	VALUE_DT_SPEC_GET_BY_IDX(node_id, prop, idx),

#define MONITOR_DEVICE(inst)						       \
									       \
	static const struct value_dt_spec monitor_values_##inst[] = {	       \
		DT_INST_FOREACH_PROP_ELEM(inst, values, MONITOR_VALUE_DT_SPEC) \
	};								       \
									       \
	static struct monitor_data monitor_data_##inst = {		       \
		.sub = VALUE_SUB_INIT(),				       \
		.fault = false,						       \
		.active = DT_INST_PROP(inst, initial_active),		       \
	};								       \
									       \
	static const struct monitor_config monitor_config_##inst = {	       \
		.values = monitor_values_##inst,			       \
		.num_values = ARRAY_SIZE(monitor_values_##inst),	       \
		.minimum = DT_INST_PROP_NUM_OR_SCALED(inst, minimum,	       \
						      VALUE_MIN, scale),       \
		.maximum = DT_INST_PROP_NUM_OR_SCALED(inst, maximum,	       \
						      VALUE_MAX, scale),       \
	};								       \
									       \
	DEVICE_DT_INST_DEFINE(inst, monitor_init, NULL, &monitor_data_##inst,  \
			      &monitor_config_##inst, POST_KERNEL,	       \
			      CONFIG_CONDITION_MONITOR_INIT_PRIORITY,	       \
			      &monitor_api);

DT_INST_FOREACH_STATUS_OKAY(MONITOR_DEVICE)
