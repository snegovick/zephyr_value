/*
 * Copyright (c) 2023 MBT
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/device.h>
#include <zephyr/drivers/value.h>
#include <zephyr/dt-bindings/value/params.h>
#include <zephyr/fixed_point.h>
#include <zephyr/logging/log.h>

#define DT_DRV_COMPAT PARAMS_DT_COMPAT

LOG_MODULE_REGISTER(params, CONFIG_VALUE_PARAMS_LOG_LEVEL);

#if IS_ENABLED(CONFIG_VALUE_PARAMS_SETTINGS)

#include <zephyr/settings/settings.h>

#define PARAMS_SETTINGS_NAME "vpar"

#define PARAMS_SETTINGS_CONFIG_FIELDS \
	const char *settings_name;

#define PARAMS_SETTINGS_INST_NAME(id) \
	PARAMS_SETTINGS_NAME "/" DT_NODE_FULL_NAME(DT_DRV_INST(id))

#define PARAMS_SETTINGS_CONFIG_FIELDS_INIT(id) \
	.settings_name = PARAMS_SETTINGS_INST_NAME(id),

#define PARAMS_SETTINGS_HANDLER_DEFINE(id)				    \
	static int params_settings_set_##id(const char *name,		    \
					    size_t len,			    \
					    settings_read_cb read_cb,	    \
					    void *cb_arg)		    \
	{								    \
		return params_settings_set(name, len, read_cb, cb_arg,	    \
					   DEVICE_DT_GET(DT_DRV_INST(id))); \
	}								    \
									    \
	SETTINGS_STATIC_HANDLER_DEFINE(params_settings_handler_##id,	    \
				       PARAMS_SETTINGS_INST_NAME(id),	    \
				       NULL, params_settings_set_##id,	    \
				       NULL, NULL)

#else /* !IS_ENABLED(CONFIG_VALUE_PARAMS_SETTINGS) */

#define PARAMS_SETTINGS_CONFIG_FIELDS
#define PARAMS_SETTINGS_CONFIG_FIELDS_INIT(inst)
#define PARAMS_SETTINGS_HANDLER_DEFINE(id)

#endif /* IS_ENABLED(CONFIG_VALUE_PARAMS_SETTINGS) */

enum param_flag {
	param_exists            = 1 << 0,
	param_non_volatile      = 1 << 1,
};

struct param_desc {
	value_t def;    /* default value */
	value_t min;    /* minimum value */
	value_t max;    /* maximum value */
	uint8_t flags;
};

struct params_config {
	PARAMS_SETTINGS_CONFIG_FIELDS
	unsigned num_params;
	struct param_desc param_desc[];
};

static unsigned params_count(const struct device *dev, uint8_t flags, uint8_t flags_mask)
{
	const struct params_config *cfg = dev->config;
	unsigned cnt;
	unsigned idx;

	for (cnt = 0, idx = 0; idx < cfg->num_params; idx++) {
		if ((cfg->param_desc[idx].flags & flags_mask) == flags) {
			cnt++;
		}
	}

	return cnt;
}

/* get parameter value by id */
static int param_get(const struct device *dev, value_id_t id, value_t *pval)
{
	const struct params_config *cfg = dev->config;
	value_t *values = dev->data;

	if (id >= cfg->num_params ||
	    !(cfg->param_desc[id].flags & param_exists)) {
		return -EINVAL;
	}

	*pval = values[id];
	return 0;
}

/* set parameter value by id */
static int param_set(const struct device *dev, value_id_t id, value_t val)
{
	const struct params_config *cfg = dev->config;
	value_t *values = dev->data;

	if (id >= cfg->num_params || !(cfg->param_desc[id].flags & param_exists)) {
		return -EINVAL;
	}

	values[id] = fixp_clamp(val, cfg->param_desc[id].min, cfg->param_desc[id].max);
	return 0;
}

#if IS_ENABLED(CONFIG_VALUE_PARAMS_SETTINGS)

struct param_pair {
	value_id_t id;
	value_t val;
};

static int params_settings_set(const char *name, size_t len,
			       settings_read_cb read_cb,
			       void *cb_arg,
			       const struct device *dev)
{
	unsigned max_params =
		params_count(dev, param_exists | param_non_volatile, ~0);
	struct param_pair pairs[max_params];
	struct param_pair *pair;
	int rc;

	rc = read_cb(cb_arg, &pairs, sizeof(pairs));
	if (rc >= 0) {
		for (pair = pairs + rc - 1; pair > pairs; pair--) {
			rc = param_set(dev, pair->id, pair->val);
			if (rc != 0) {
				LOG_WRN("%s: Error when load parameter: %u",
					dev->name, pair->id);
			}
		}
	}

	return rc;
}

static inline int params_load(const struct device *dev)
{
	const struct params_config *cfg = dev->config;
	int rc;

	rc = settings_load_subtree(cfg->settings_name);
	if (rc < 0) {
		LOG_ERR("Load params failed: %d", rc);
	}

	return rc;
}

static inline int params_save(const struct device *dev)
{
	unsigned num_params =
		params_count(dev, param_exists | param_non_volatile, ~0);
	const struct params_config *cfg = dev->config;
	value_t *values = dev->data;
	struct param_pair pairs[num_params];
	struct param_pair *pair = pairs + num_params - 1;
	unsigned idx;
	int rc;

	for (idx = 0; idx < cfg->num_params; idx++) {
		if (cfg->param_desc[idx].flags ==
		    (param_exists | param_non_volatile)) {
			pair->id = idx;
			pair->val = values[idx];
			pair--;
		}
	}

	rc = settings_save_one(cfg->settings_name,
			       pairs, sizeof(pairs));

	if (rc < 0) {
		LOG_WRN("Save params failed: %d", rc);
	}

	return rc;
}

#endif /* IS_ENABLED(CONFIG_VALUE_PARAMS_SETTINGS) */

static inline void params_reset(const struct device *dev)
{
	const struct params_config *cfg = dev->config;
	value_t *values = dev->data;
	unsigned idx;

	for (idx = 0; idx < cfg->num_params; idx++) {
		if (cfg->param_desc[idx].flags & param_exists) {
			values[idx] = cfg->param_desc[idx].def;
		}
	}
}

static int params_value_get(const struct device *dev, value_id_t id, value_t *pval)
{
	int rc = 0;

	switch (id) {
	case PARAMS_NUMBER_ALL:
		*pval = params_count(dev, param_exists, param_exists);
		break;

	case PARAMS_NUMBER_NV:
		*pval = params_count(dev, param_exists | param_non_volatile, ~0);
		break;

	default:
		rc = param_get(dev, id, pval);

		if (rc == 0) {
			break;
		}

		LOG_ERR("%s: attempt to get unknown value #%d", dev->name, id);
	}

	return rc;
}

static int params_value_set(const struct device *dev, value_id_t id, value_t val)
{
	int rc = 0;

	switch (id) {
	case PARAMS_COMMAND:
		switch (val) {
#if IS_ENABLED(CONFIG_VALUE_PARAMS_SETTINGS)
		case PARAMS_LOAD:
			rc = params_load(dev);
			break;
		case PARAMS_SAVE:
			rc = params_save(dev);
			break;
#endif /* IS_ENABLED(CONFIG_VALUE_PARAMS_SETTINGS) */
		case PARAMS_RESET:
			params_reset(dev);
			break;
		default:
			LOG_ERR("%s: attempt to invoke unknown command #%d", dev->name, val);
			rc = -EINVAL;
		}
		break;

	default:
		rc = param_set(dev, id, val);

		if (rc == 0) {
			break;
		}

		LOG_ERR("%s: attempt to set unknown value #%d", dev->name, id);
	}

	return rc;
}

static const struct value_driver_api params_api = {
	.get = params_value_get,
	.set = params_value_set,
};

static int params_init(const struct device *dev)
{
	params_reset(dev);

#if IS_ENABLED(CONFIG_VALUE_PARAMS_SETTINGS)
	return params_load(dev);
#else /* !IS_ENABLED(CONFIG_VALUE_PARAMS_SETTINGS) */
	return 0;
#endif /* IS_ENABLED(CONFIG_VALUE_PARAMS_SETTINGS) */
}

#define _PARAM_LIM(node_id, prop, default_value)		\
	COND_CODE_1(DT_NODE_HAS_PROP(node_id, prop),		\
		    (FIXP_DT_PROP_SCALE(node_id, prop, scale)),	\
		    (default_value))

#define _PARAMS_DESC(node_id)					    \
	[DT_PROP(node_id, id)] = {				    \
		.def = FIXP_DT_PROP_SCALE(node_id, value, scale),   \
		.min = _PARAM_LIM(node_id, min, VALUE_MIN),	    \
		.max = _PARAM_LIM(node_id, max, VALUE_MAX),	    \
		.flags = param_exists				    \
			 IF_ENABLED(DT_PROP(node_id, non_volatile), \
				    (| param_non_volatile)),	    \
	},

#define _PARAMS_VALUE(node_id) \
	[DT_PROP(node_id, id)] = 0,

#define PARAMS_DEVICE(id)					 \
	PARAMS_SETTINGS_HANDLER_DEFINE(id);			 \
								 \
	value_t params_data_##id[] = {				 \
		DT_INST_FOREACH_CHILD(id, _PARAMS_VALUE)	 \
	};							 \
								 \
	static const struct params_config params_config_##id = { \
		PARAMS_SETTINGS_CONFIG_FIELDS_INIT(id)		 \
		.num_params = ARRAY_SIZE(params_data_##id),	 \
		.param_desc = {					 \
			DT_INST_FOREACH_CHILD(id, _PARAMS_DESC)	 \
		},						 \
	};							 \
								 \
	DEVICE_DT_INST_DEFINE(id, params_init, NULL,		 \
			      &params_data_##id,		 \
			      &params_config_##id, POST_KERNEL,	 \
			      CONFIG_VALUE_PARAMS_INIT_PRIORITY, \
			      &params_api);

DT_INST_FOREACH_STATUS_OKAY(PARAMS_DEVICE)
