/*
 * Copyright (c) 2023 MBT
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/device.h>
#include <zephyr/drivers/value.h>
#include <zephyr/dt-bindings/value/filter.h>
#include <zephyr/fixed_point.h>
#include <zephyr/logging/log.h>

#define DT_DRV_COMPAT FILTER_DT_COMPAT

LOG_MODULE_REGISTER(filter, CONFIG_VALUE_FILTER_LOG_LEVEL);

#if IS_ENABLED(CONFIG_VALUE_FILTER_SETTINGS)

#include <zephyr/settings/settings.h>

#define FILTER_SETTINGS_NAME "vflt"

#define FILTER_SETTINGS_CONFIG_FIELDS \
	const char *settings_name;

#define FILTER_SETTINGS_INST_NAME(id) \
	FILTER_SETTINGS_NAME "/" DT_NODE_FULL_NAME(DT_DRV_INST(id))

#define FILTER_SETTINGS_CONFIG_FIELDS_INIT(id) \
	.settings_name = FILTER_SETTINGS_INST_NAME(id),

#define FILTER_SETTINGS_HANDLER_DEFINE(id)				    \
	static int filter_settings_set_##id(const char *name,		    \
					    size_t len,			    \
					    settings_read_cb read_cb,	    \
					    void *cb_arg)		    \
	{								    \
		return filter_settings_set(name, len, read_cb, cb_arg,	    \
					   DEVICE_DT_GET(DT_DRV_INST(id))); \
	}								    \
									    \
	SETTINGS_STATIC_HANDLER_DEFINE(filter_settings_handler_##id,	    \
				       FILTER_SETTINGS_INST_NAME(id),	    \
				       NULL, filter_settings_set_##id,	    \
				       NULL, NULL)

#else /* !IS_ENABLED(CONFIG_VALUE_FILTER_SETTINGS) */

#define FILTER_SETTINGS_CONFIG_FIELDS
#define FILTER_SETTINGS_CONFIG_FIELDS_INIT(inst)
#define FILTER_SETTINGS_HANDLER_DEFINE(id)

#endif /* IS_ENABLED(CONFIG_VALUE_FILTER_SETTINGS) */

struct filter_param {
	value_t alpha;
	value_t one_minus_alpha;
};

#define MAX_FLAG_BYTES ((CONFIG_VALUE_FILTER_MAX_VALUES * 2 + 7) / 8)

#define FILTER_DATA_STRUCT(type_name, num_values) \
	struct type_name {			  \
		struct filter_param param;	  \
		bool active;			  \
		uint8_t flags[MAX_FLAG_BYTES];	  \
		value_t values[num_values];	  \
	}

FILTER_DATA_STRUCT(filter_data, 0);

typedef value_t filter_calc(const struct filter_param *param,
			    value_t value, value_t prev_value,
			    bool ready);

#define FILTER_CONFIG_STRUCT(type_name, num_values_)	  \
	struct type_name {				  \
		FILTER_SETTINGS_CONFIG_FIELDS		  \
		filter_calc *calculate;			  \
		value_t default_alpha;			  \
		value_t period;				  \
		value_t param_scale;			  \
		uint16_t num_values;			  \
		struct value_dt_spec values[num_values_]; \
	}

FILTER_CONFIG_STRUCT(filter_config, 0);

#if IS_ENABLED(CONFIG_VALUE_FILTER_SETTINGS)

static int filter_set_alpha(const struct device *dev, value_t alpha);

static int filter_settings_set(const char *name, size_t len,
			       settings_read_cb read_cb, void *cb_arg,
			       const struct device *dev)
{
	value_t alpha;
	int rc;

	rc = read_cb(cb_arg, &alpha, sizeof(alpha));
	if (rc >= 0) {
		return filter_set_alpha(dev, alpha);
	}

	return rc;
}

static inline int filter_param_load(const struct device *dev)
{
	const struct filter_config *cfg = dev->config;
	int rc;

	rc = settings_load_subtree(cfg->settings_name);
	if (rc < 0) {
		LOG_ERR("Load filter parameter failed: %d", rc);
	}

	return rc;
}

static inline int filter_param_save(const struct device *dev)
{
	const struct filter_config *cfg = dev->config;
	struct filter_data *data = dev->data;
	int rc;

	rc = settings_save_one(cfg->settings_name,
			       data->param.alpha != cfg->default_alpha ?
			       &data->param.alpha : NULL,
			       sizeof(data->param.alpha));

	if (rc < 0) {
		LOG_WRN("Save filter parameter failed: %d", rc);
	}

	return rc;
}

#endif /* IS_ENABLED(CONFIG_VALUE_FILTER_SETTINGS) */

static inline void filter_param_reset(const struct device *dev)
{
	const struct filter_config *cfg = dev->config;
	struct filter_data *data = dev->data;

	data->param.alpha = cfg->default_alpha;
}

static inline bool is_ready(const uint8_t *data, unsigned bit)
{
	return (data[bit / 4] >> ((bit % 4) * 2)) & 1;
}

static inline void set_ready(uint8_t *data, unsigned bit)
{
	data[bit / 4] |= 1 << ((bit % 4) * 2);
}

static inline void reset_ready(uint8_t *data, unsigned bit)
{
	data[bit / 4] &= ~(1 << ((bit % 4) * 2));
}

static inline bool is_fault(const uint8_t *data, unsigned bit)
{
	return (data[bit / 4] >> ((bit % 4) * 2)) & 2;
}

static inline void set_fault(uint8_t *data, unsigned bit)
{
	data[bit / 4] |= 2 << ((bit % 4) * 2);
}

static inline void reset_fault(uint8_t *data, unsigned bit)
{
	data[bit / 4] &= ~(2 << ((bit % 4) * 2));
}

static inline void reset_flags(uint8_t *data)
{
	memset(data, 0, MAX_FLAG_BYTES);
}

static void filter_task(const struct device *dev)
{
	const struct filter_config *cfg = dev->config;
	struct filter_data *data = dev->data;
	value_t value;
	unsigned idx;
	int rc;

	for (idx = 0; idx < cfg->num_values; idx++) {
		rc = value_get_dt(&cfg->values[idx], &value);
		if (rc != 0) {
			reset_ready(data->flags, idx);
			if (rc != -EAGAIN) {
				set_fault(data->flags, idx);
			}
			continue;
		}

		data->values[idx] = cfg->calculate(&data->param, value,
						   data->values[idx],
						   is_ready(data->flags, idx));
		set_ready(data->flags, idx);
		reset_fault(data->flags, idx);
	}
}

static int filter_value_get(const struct device *dev, value_id_t id, value_t *pval)
{
	const struct filter_config *cfg = dev->config;
	struct filter_data *data = dev->data;
	int rc = 0;

	switch (id) {
	case FILTER_STATE:
		*pval = data->active;
		break;

	case FILTER_ALPHA:
		*pval = data->param.alpha;
		break;

	case FILTER_SAMPLES:
		/* get number of smoothing samples from alpha using formula: 2 / alpha - 1 */
		*pval = (value_t)((int64_t)cfg->param_scale *
				  (int64_t)cfg->param_scale * 2 /
				  data->param.alpha) - cfg->param_scale;
		break;

	case FILTER_WINDOW:
		/* get swoothing time window from alpha using formula: (2 / alpha - 1) * period */
		*pval = (int64_t)((value_t)((int64_t)cfg->param_scale *
					    (int64_t)cfg->param_scale * 2 /
					    data->param.alpha) - cfg->param_scale) *
			cfg->period / cfg->param_scale;
		break;

	case FILTER_PERIOD:
		*pval = cfg->period;
		break;

	case FILTER_VALUES:
		*pval = cfg->num_values;
		break;

	default:
		if (id < cfg->num_values) {
			*pval = data->values[id];
			if (is_fault(data->flags, id)) {
				rc = -EFAULT;
			} else if (!is_ready(data->flags, id)) {
				rc = -EAGAIN;
			}
			break;
		}

		LOG_ERR("%s: attempt to get unknown value #%u", dev->name, id);
		rc = -EINVAL;
	}

	return rc;
}

static int filter_set_alpha(const struct device *dev, value_t alpha)
{
	const struct filter_config *cfg = dev->config;
	struct filter_data *data = dev->data;

	if (alpha == data->param.alpha) {
		// nothing to do
		return 0;
	}

	/* check that alpha is in range 0 .. 1 */
	if (alpha < 0 || alpha > cfg->param_scale) {
		LOG_ERR("%s: attempt to set invalid alpha value %d/%d", dev->name,
			alpha, cfg->param_scale);
		return -EINVAL;
	}

	data->param.alpha = alpha;
	data->param.one_minus_alpha = cfg->param_scale - alpha;

	return 0;
}

static int filter_value_set(const struct device *dev, value_id_t id, value_t val)
{
	const struct filter_config *cfg = dev->config;
	struct filter_data *data = dev->data;
	int rc = 0;

	switch (id) {
	case FILTER_STATE:
		if (val == data->active) {
			break;
		}

		data->active = val;
		reset_flags(data->flags);
		break;

	case FILTER_ALPHA:
		rc = filter_set_alpha(dev, val);
		break;

	case FILTER_SAMPLES:
		/* set alpha from number of smoothing samples using formula: 2 / (n + 1) */
		rc = filter_set_alpha(dev, (int64_t)cfg->param_scale *
				      (int64_t)cfg->param_scale * 2 /
				      (val + cfg->param_scale));
		break;

	case FILTER_WINDOW:
		/* set alpha from smoothing time window using formula: 2 / (T / P + 1) */
		rc = filter_set_alpha(dev, (int64_t)cfg->param_scale *
				      (int64_t)cfg->param_scale * 2 /
				      ((int64_t)val * cfg->param_scale /
				       cfg->period +
				       cfg->param_scale));
		break;

	case FILTER_SYNC:
		/* do synchronization */
		if (data->active) {
			filter_task(dev);
		}
		break;

	case FILTER_COMMAND:
		/* command invocation */
		switch (val) {
#if IS_ENABLED(CONFIG_VALUE_FILTER_SETTINGS)
		case FILTER_PARAM_LOAD:
			rc = filter_param_load(dev);
			break;
		case FILTER_PARAM_SAVE:
			rc = filter_param_save(dev);
			break;
#endif /* IS_ENABLED(CONFIG_VALUE_FILTER_SETTINGS) */
		case FILTER_PARAM_RESET:
			filter_param_reset(dev);
			break;
		default:
			LOG_ERR("%s: attempt to invoke unknown command #%d",
				dev->name, val);
			rc = -EINVAL;
		}
		break;

	default:
		LOG_ERR("%s: attempt to set unknown value #%d", dev->name, id);
		rc = -EINVAL;
	}

	return rc;
}

static const struct value_driver_api filter_api = {
	.get = filter_value_get,
	.set = filter_value_set,
};

static int filter_init(const struct device *dev)
{
#if IS_ENABLED(CONFIG_VALUE_FILTER_SETTINGS)
	return filter_param_load(dev);
#else /* !IS_ENABLED(CONFIG_VALUE_FILTER_SETTINGS) */
	return 0;
#endif /* IS_ENABLED(CONFIG_VALUE_FILTER_SETTINGS) */
}

#define _CALC_PERIOD(id) FIXP_DT_INST_PROP_SCALE(id, period, param_scale)

#define _PARAM_SCALE(id) DT_INST_PROP(id, param_scale)

/* set alpha and 1 - alpha */
#define _SET_PARAM_ALPHA(id, alpha_)		     \
	{					     \
		.alpha = (alpha_),		     \
		.one_minus_alpha =		     \
			_PARAM_SCALE(id) - (alpha_), \
	}

/* convert number of smoothing samples to alpha using formula: 2 / (n + 1) */
#define _SAMPLES_TO_ALPHA(id, samples)	 \
	((int64_t)_PARAM_SCALE(id) *	 \
	 (int64_t)_PARAM_SCALE(id) * 2 / \
	 ((samples) + _PARAM_SCALE(id)))

/* convert smoothing period to alpha using formula: 2 / (T / P + 1) */
#define _WINDOW_TO_ALPHA(id, window)				 \
	_SAMPLES_TO_ALPHA(id,					 \
			  (int64_t)(window) * _PARAM_SCALE(id) / \
			  _CALC_PERIOD(id))

#define COND_CODE_1_X3(cond1, expr1, cond2, expr2, cond3, expr3, def) \
	COND_CODE_1(cond1, expr1,				      \
		    (COND_CODE_1(cond2, expr2,			      \
				 (COND_CODE_1(cond3, expr3, def)))))

#define _GET_SAMPLES_AS_ALPHA(id)			       \
	_SAMPLES_TO_ALPHA(id,				       \
			  FIXP_DT_INST_PROP_SCALE(id, samples, \
						  param_scale))

#define _GET_WINDOW_AS_ALPHA(id)			     \
	_WINDOW_TO_ALPHA(id,				     \
			 FIXP_DT_INST_PROP_SCALE(id, window, \
						 param_scale))

#define _GET_PARAM_AS_ALPHA(id)				       \
	COND_CODE_1_X3(DT_INST_NODE_HAS_PROP(id, alpha),       \
		       (FIXP_DT_INST_PROP_SCALE(id, alpha,     \
						param_scale)), \
		       DT_INST_NODE_HAS_PROP(id, samples),     \
		       (_GET_SAMPLES_AS_ALPHA(id)),	       \
		       DT_INST_NODE_HAS_PROP(id, window),      \
		       (_GET_WINDOW_AS_ALPHA(id)),	       \
		       (1.0))

#define _NUM_VALUES(id)	\
	DT_INST_PROP_LEN(id, values)

#define _VALUE_SPEC(node_id, prop, idx)	\
	VALUE_DT_SPEC_GET_BY_IDX(node_id, prop, idx),

#define FILTER_DEVICE(id)						      \
	FILTER_SETTINGS_HANDLER_DEFINE(id);				      \
									      \
	static value_t filter_calc_##id(const struct filter_param *param,     \
					value_t value, value_t prev_value,    \
					bool ready) {			      \
		int64_t value_scaled = (int64_t)value *			      \
				       DT_INST_PROP_OR(id, output_scale, 1) / \
				       DT_INST_PROP_OR(id, input_scale, 1);   \
									      \
		return ready ?						      \
		       (value_scaled * (int64_t)param->alpha +		      \
			(int64_t)prev_value *				      \
			(int64_t)param->one_minus_alpha) /		      \
		       _PARAM_SCALE(id) :				      \
		       value_scaled;					      \
	}								      \
									      \
	static FILTER_DATA_STRUCT(, _NUM_VALUES(id))			      \
	filter_data_##id = {						      \
		.param = _SET_PARAM_ALPHA(id, _GET_PARAM_AS_ALPHA(id)),	      \
		.active = DT_INST_PROP(id, initial_active),		      \
	};								      \
									      \
	static const FILTER_CONFIG_STRUCT(, _NUM_VALUES(id))		      \
	filter_config_##id = {						      \
		FILTER_SETTINGS_CONFIG_FIELDS_INIT(id)			      \
		.num_values = _NUM_VALUES(id),				      \
		.param_scale = _PARAM_SCALE(id),			      \
		.calculate = filter_calc_##id,				      \
		.default_alpha = _GET_PARAM_AS_ALPHA(id),		      \
		.period = _CALC_PERIOD(id),				      \
		.values = {						      \
			DT_INST_FOREACH_PROP_ELEM(id, values, _VALUE_SPEC)    \
		},							      \
	};								      \
									      \
	DEVICE_DT_INST_DEFINE(id, filter_init, NULL, &filter_data_##id,	      \
			      &filter_config_##id, POST_KERNEL,		      \
			      CONFIG_VALUE_FILTER_INIT_PRIORITY, &filter_api);

DT_INST_FOREACH_STATUS_OKAY(FILTER_DEVICE)
