/*
 * Copyright (c) 2023 MBT
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/dt-bindings/value/sync.h>
#include <zephyr/drivers/value.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/logging/log.h>
#include <zephyr/kernel.h>

#if IS_ENABLED(CONFIG_VALUE_SYNC_TIMING)
#include <timing/timing.h>
#endif /* IS_ENABLED(CONFIG_VALUE_SYNC_TIMING) */

#define DT_DRV_COMPAT SYNC_DT_COMPAT

LOG_MODULE_REGISTER(sync, CONFIG_VALUE_SYNC_LOG_LEVEL);

struct sync_data {
	/* control loop work */
	struct k_work_delayable work;
	/* pointer to device (needed for delayable work) */
	const struct device *dev;
#if IS_ENABLED(CONFIG_VALUE_SYNC_TIMING)
	uint32_t min_cycles;
	uint32_t max_cycles;
#endif
};

struct sync_config {
	/* sync values */
	const struct value_dt_spec *values;
	uint32_t sync_period;
	uint8_t num_values;
	bool initial_active;
};

static bool sync_is_active(const struct device *dev)
{
	struct sync_data *data = dev->data;

	return k_work_delayable_is_pending(&data->work);
}

static int sync_set_active(const struct device *dev, bool active)
{
	const struct sync_config *cfg = dev->config;
	struct sync_data *data = dev->data;

	if (active == sync_is_active(dev)) {
		return 0;
	}

	return active ?
	       /* start syncing loop */
	       k_work_schedule(&data->work, K_MSEC(cfg->sync_period)) :
	       /* stop syncing loop */
	       k_work_cancel_delayable(&data->work);
}

static int sync_value_get(const struct device *dev, value_id_t id, value_t *pval)
{
#if IS_ENABLED(CONFIG_VALUE_SYNC_TIMING)
	struct sync_data *data = dev->data;
#endif /* IS_ENABLED(CONFIG_VALUE_SYNC_TIMING) */

	int status = 0;

	switch (id) {
	case SYNC_STATE:
		*pval = sync_is_active(dev);
		break;

#if IS_ENABLED(CONFIG_VALUE_SYNC_TIMING)
	case SYNC_MIN_CYCLES:
		*pval = data->min_cycles;
		break;
	case SYNC_MAX_CYCLES:
		*pval = data->max_cycles;
		break;
#endif /* IS_ENABLED(CONFIG_VALUE_SYNC_TIMING) */

	default:
		LOG_ERR("%s: attempt to get unknown value #%d", dev->name, id);
		status = -EINVAL;
	}

	return status;
}

static int sync_value_set(const struct device *dev, value_id_t id, value_t val)
{
	int status = 0;

	switch (id) {
	case SYNC_STATE:
		status = sync_set_active(dev, val);
		if (status > 0) {
			status = 0;
		}
		break;

	default:
		LOG_ERR("%s: attempt to set unknown value #%d", dev->name, id);
		status = -EINVAL;
	}

	return status;
}

static const struct value_driver_api sync_api = {
	.get = sync_value_get,
	.set = sync_value_set,
};

static void sync_work(struct k_work *work)
{
	struct k_work_delayable *kwd = k_work_delayable_from_work(work);
	struct sync_data *data = CONTAINER_OF(work, struct sync_data, work);
	const struct device *dev = data->dev;
	const struct sync_config *cfg = dev->config;
	const struct value_dt_spec *value = &cfg->values[0];
	const struct value_dt_spec *value_end = &cfg->values[cfg->num_values];

#if IS_ENABLED(CONFIG_VALUE_SYNC_TIMING)
	timing_t start_time;
	timing_t end_time;
	uint64_t cycles;
#endif /* IS_ENABLED(CONFIG_VALUE_SYNC_TIMING) */

	if (!(k_work_delayable_busy_get(&data->work) & K_WORK_CANCELING)) {
		/* re-schedule control loop work when active */
		k_work_schedule(&data->work, K_MSEC(cfg->sync_period));
	}

#if IS_ENABLED(CONFIG_VALUE_SYNC_TIMING)
	start_time = timing_counter_get();
#endif /* IS_ENABLED(CONFIG_VALUE_SYNC_TIMING) */

	for (; value < value_end; value++) {
		value_set_dt(value, cfg->sync_period);
	}

#if IS_ENABLED(CONFIG_VALUE_SYNC_TIMING)
	end_time = timing_counter_get();

	cycles = timing_cycles_get(&start_time, &end_time);

	if (data->min_cycles == 0 || cycles < data->min_cycles) {
		data->min_cycles = cycles;
	}
	if (data->max_cycles == 0 || cycles > data->max_cycles) {
		data->max_cycles = cycles;
	}
#endif /* IS_ENABLED(CONFIG_VALUE_SYNC_TIMING) */
}

static int sync_init(const struct device *dev)
{
	const struct sync_config *cfg = dev->config;
	struct sync_data *data = dev->data;
	int status = 0;

	k_work_init_delayable(&data->work, sync_work);

	if (cfg->initial_active) {
		status = k_work_schedule(&data->work, K_MSEC(cfg->sync_period));
	}

	return status;
}

#define SYNC_VALUE_DT_SPEC(node_id, prop, idx) \
	VALUE_DT_SPEC_GET_BY_IDX(node_id, prop, idx),

#define SYNC_DEVICE(id)							  \
									  \
	static const struct value_dt_spec sync_values_##id[] = {	  \
		DT_INST_FOREACH_PROP_ELEM(id, values, SYNC_VALUE_DT_SPEC) \
	};								  \
									  \
	static struct sync_data sync_data_##id = {			  \
		.dev = DEVICE_DT_GET(DT_DRV_INST(id)),			  \
	};								  \
									  \
	static const struct sync_config sync_config_##id = {		  \
		.values = sync_values_##id,				  \
		.num_values = ARRAY_SIZE(sync_values_##id),		  \
		.sync_period = DT_INST_PROP_OR(id, period, 1000),	  \
		.initial_active = DT_INST_PROP(id, initial_active),	  \
	};								  \
									  \
	DEVICE_DT_INST_DEFINE(id, sync_init, NULL, &sync_data_##id,	  \
			      &sync_config_##id, POST_KERNEL,		  \
			      CONFIG_VALUE_SYNC_INIT_PRIORITY, &sync_api);

DT_INST_FOREACH_STATUS_OKAY(SYNC_DEVICE)
