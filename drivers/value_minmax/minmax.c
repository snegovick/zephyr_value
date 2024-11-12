/*
 * Copyright (c) 2023 MBT
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>
#include <zephyr/device.h>
#include <zephyr/drivers/value.h>
#include <zephyr/dt-bindings/value/minmax.h>
#include <zephyr/logging/log.h>

#define DT_DRV_COMPAT MINMAX_DT_COMPAT

LOG_MODULE_REGISTER(minmax, CONFIG_MINMAX_LOG_LEVEL);

struct minmax_entry {
	value_t minimum;
	value_t maximum;
};

#define MINMAX_READY_BYTES (CONFIG_MINMAX_MAX_VALUES + 7) / 8

struct minmax_data {
	bool active;
	uint8_t ready[MINMAX_READY_BYTES];
	struct minmax_entry entries[];
};

struct minmax_config {
	unsigned num_values;
	struct value_dt_spec values[];
};

static inline bool is_flag(const uint8_t *data, unsigned bit)
{
	return (data[bit / 8] >> (bit % 8)) & 1;
}

static inline void set_flag(uint8_t *data, unsigned bit)
{
	data[bit / 8] |= 1 << (bit % 8);
}

static inline void reset_flags(uint8_t *data)
{
	memset(data, 0, MINMAX_READY_BYTES);
}

static void minmax_task(const struct device *dev)
{
	const struct minmax_config *cfg = dev->config;
	struct minmax_data *data = dev->data;
	struct minmax_entry *entry;
	value_t value;
	unsigned ch;

	for (ch = 0; ch < cfg->num_values; ch++) {
		if (value_get_dt(&cfg->values[ch], &value)) {
			continue;
		}

		entry = &data->entries[ch];

		if (is_flag(data->ready, ch)) {
			if (value < entry->minimum) {
				entry->minimum = value;
			}
			if (value > entry->maximum) {
				entry->maximum = value;
			}
		} else {
			entry->minimum = value;
			entry->maximum = value;
		}

		set_flag(data->ready, ch);
	}
}

static int minmax_value_get(const struct device *dev, value_id_t id, value_t *pval)
{
	const struct minmax_config *cfg = dev->config;
	struct minmax_data *data = dev->data;
	struct minmax_entry *entry;
	int ch;
	int type;
	int rc = 0;

	switch (id) {
	case MINMAX_STATE:
		*pval = data->active;
		break;

	default:
		ch = MINMAX_CH_IDX(id);

		if (ch >= 0 && ch < cfg->num_values) {
			type = MINMAX_CH_TYPE(id);
			entry = &data->entries[ch];

			*pval = type == MINMAX_CH_TYPE_MIN ?
				entry->minimum : entry->maximum;

			if (!is_flag(data->ready, ch)) {
				rc = -EAGAIN;
			}

			break;
		}

		LOG_ERR("%s: attempt to get unknown value #%d", dev->name, id);
		rc = -EINVAL;
	}

	return rc;
}

static int minmax_value_set(const struct device *dev, value_id_t id, value_t val)
{
	struct minmax_data *data = dev->data;
	int rc = 0;

	switch (id) {
	case MINMAX_STATE:
		if (val == data->active) {
			break;
		}

		data->active = val;
		reset_flags(data->ready);

		break;

	case MINMAX_SYNC:
		/* do synchronization */
		if (data->active) {
			minmax_task(dev);
		}
		break;

	default:
		LOG_ERR("%s: attempt to set unknown value #%d", dev->name, id);
		rc = -EINVAL;
	}

	return rc;
}

static const struct value_driver_api minmax_api = {
	.get = minmax_value_get,
	.set = minmax_value_set,
};

static int minmax_init(const struct device *dev)
{
	return 0;
}

#define _MINMAX_ENTRY(node_id, prop, idx) \
	{ .minimum = 0, .maximum = 0, },

#define _MINMAX_SPEC(node_id, prop, idx) \
	VALUE_DT_SPEC_GET_BY_IDX(node_id, prop, idx),

#define MINMAX_DEVICE(id)						     \
	BUILD_ASSERT(DT_INST_PROP_LEN(id, values) <=			     \
		     CONFIG_MINMAX_MAX_VALUES,				     \
		     "Too many values configured! "			     \
		     "Try set config MINMAX_MAX_VALUES.");		     \
									     \
	static struct minmax_data minmax_data_##id = {			     \
		.active = DT_INST_PROP(id, initial_active),		     \
		.entries = {						     \
			DT_INST_FOREACH_PROP_ELEM(id, values, _MINMAX_ENTRY) \
		},							     \
	};								     \
									     \
	static const struct minmax_config minmax_config_##id = {	     \
		.num_values = DT_INST_PROP_LEN(id, values),		     \
		.values = {						     \
			DT_INST_FOREACH_PROP_ELEM(id, values, _MINMAX_SPEC)  \
		},							     \
	};								     \
									     \
	DEVICE_DT_INST_DEFINE(id, minmax_init, NULL, &minmax_data_##id,	     \
			      &minmax_config_##id, POST_KERNEL,		     \
			      CONFIG_MINMAX_INIT_PRIORITY, &minmax_api);

DT_INST_FOREACH_STATUS_OKAY(MINMAX_DEVICE)
