/*
 * Copyright (c) 2023 MBT
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/device.h>
#include <zephyr/drivers/value.h>
#include <zephyr/dt-bindings/value/mix.h>
#include <zephyr/fixed_point.h>
#include <zephyr/logging/log.h>

#define DT_DRV_COMPAT MIX_DT_COMPAT

LOG_MODULE_REGISTER(mix, CONFIG_VALUE_MIX_LOG_LEVEL);

#if IS_ENABLED(CONFIG_VALUE_MIX_SETTINGS)

#include <zephyr/settings/settings.h>

#define MIX_SETTINGS_NAME "vmix"

#define MIX_SETTINGS_CONFIG_FIELDS \
	const char *settings_name;

#define MIX_SETTINGS_INST_NAME(id) \
	MIX_SETTINGS_NAME "/" DT_NODE_FULL_NAME(DT_DRV_INST(id))

#define MIX_SETTINGS_CONFIG_FIELDS_INIT(id) \
	.settings_name = MIX_SETTINGS_INST_NAME(id),

#define MIX_SETTINGS_HANDLER_DEFINE(id)					 \
	static int mix_settings_set_##id(const char *name,		 \
					 size_t len,			 \
					 settings_read_cb read_cb,	 \
					 void *cb_arg)			 \
	{								 \
		return mix_settings_set(name, len, read_cb, cb_arg,	 \
					DEVICE_DT_GET(DT_DRV_INST(id))); \
	}								 \
									 \
	SETTINGS_STATIC_HANDLER_DEFINE(mix_settings_handler_##id,	 \
				       MIX_SETTINGS_INST_NAME(id),	 \
				       NULL, mix_settings_set_##id,	 \
				       NULL, NULL)

#else /* !IS_ENABLED(CONFIG_VALUE_MIX_SETTINGS) */

#define MIX_SETTINGS_CONFIG_FIELDS
#define MIX_SETTINGS_CONFIG_FIELDS_INIT(inst)
#define MIX_SETTINGS_HANDLER_DEFINE(id)

#endif /* IS_ENABLED(CONFIG_VALUE_MIX_SETTINGS) */

#define MIX_DATA_STRUCT(type_name, num_values_)	\
	struct type_name {			\
		bool active;			\
		bool ready;			\
		value_t output;			\
		value_t weights[num_values_];	\
	}

MIX_DATA_STRUCT(mix_data, 0);

struct mix_input {
	struct value_dt_spec value_spec;
	value_t default_weight;
};

#define MIX_CONFIG_STRUCT(type_name, num_values_)		      \
	struct type_name {					      \
		MIX_SETTINGS_CONFIG_FIELDS			      \
		int (*calc)(const struct mix_input *inputs,	      \
			    const value_t *weights, value_t *output); \
		unsigned num_inputs;				      \
		struct mix_input inputs[num_values_];		      \
	}

MIX_CONFIG_STRUCT(mix_config, 0);

#if IS_ENABLED(CONFIG_VALUE_MIX_SETTINGS)

static int mix_settings_set(const char *name, size_t len,
			    settings_read_cb read_cb, void *cb_arg,
			    const struct device *dev)
{
	const struct mix_config *cfg = dev->config;
	struct mix_data *data = dev->data;
	int rc;

	rc = read_cb(cb_arg, data->weights,
		     sizeof(value_t) * cfg->num_inputs);

	if (rc < 0) {
		LOG_ERR("Error when loading weights: %d", rc);
	} else if (rc != sizeof(value_t) * cfg->num_inputs) {
		LOG_WRN("Unexpected number of loaded weights");
		rc = -EINVAL;
	} else {
		rc = 0;
	}

	return rc;
}

static inline int mix_weights_load(const struct device *dev)
{
	const struct mix_config *cfg = dev->config;
	int rc;

	rc = settings_load_subtree(cfg->settings_name);
	if (rc < 0) {
		LOG_ERR("Load weights failed: %d", rc);
	}

	return rc;
}

static inline int mix_weights_save(const struct device *dev)
{
	const struct mix_config *cfg = dev->config;
	struct mix_data *data = dev->data;
	int rc;

	rc = settings_save_one(cfg->settings_name,
			       &data->weights,
			       sizeof(value_t) *
			       cfg->num_inputs);

	if (rc < 0) {
		LOG_WRN("Save weights failed: %d", rc);
	}

	return rc;
}

#endif /* IS_ENABLED(CONFIG_VALUE_MIX_SETTINGS) */

static inline void mix_weights_reset(const struct device *dev)
{
	const struct mix_config *cfg = dev->config;
	struct mix_data *data = dev->data;
	unsigned idx;

	for (idx = 0; idx < cfg->num_inputs; idx++) {
		data->weights[idx] = cfg->inputs[idx].default_weight;
	}
}

static void mix_task(const struct device *dev)
{
	const struct mix_config *cfg = dev->config;
	struct mix_data *data = dev->data;

	data->ready = 0 == cfg->calc(cfg->inputs, data->weights, &data->output);
}

static int mix_value_get(const struct device *dev, value_id_t id, value_t *pval)
{
	const struct mix_config *cfg = dev->config;
	struct mix_data *data = dev->data;
	int rc = 0;

	switch (id) {
	case MIX_STATE:
		*pval = data->active;
		break;

	case MIX_OUTPUT:
		*pval = data->output;

		if (!data->ready) {
			rc = -EAGAIN;
		}

		break;

	case MIX_INPUTS:
		*pval = cfg->num_inputs;
		break;

	default:
		if (id < cfg->num_inputs) {
			*pval = data->weights[id];
			break;
		}

		LOG_ERR("%s: attempt to get unknown value #%d", dev->name, id);
		rc = -EINVAL;
	}

	return rc;
}

static int mix_value_set(const struct device *dev, value_id_t id, value_t val)
{
	const struct mix_config *cfg = dev->config;
	struct mix_data *data = dev->data;
	int rc = 0;

	switch (id) {
	case MIX_STATE:
		if (val == data->active) {
			break;
		}

		data->active = val;
		data->ready = false;

		break;

	case MIX_SYNC:
		/* do synchronization */
		if (data->active) {
			mix_task(dev);
		}
		break;

	case MIX_COMMAND:       /* commands invocation */
		switch (val) {
#if IS_ENABLED(CONFIG_VALUE_MIX_SETTINGS)
		case MIX_WEIGHTS_LOAD:
			rc = mix_weights_load(dev);
			break;
		case MIX_WEIGHTS_SAVE:
			rc = mix_weights_save(dev);
			break;
#endif /* IS_ENABLED(CONFIG_VALUE_MIX_SETTINGS) */
		case MIX_WEIGHTS_RESET:
			mix_weights_reset(dev);
			break;
		default:
			LOG_ERR("%s: attempt to invoke unknown command #%d", dev->name, val);
			rc = -EINVAL;
		}
		break;

	default:
		if (id < cfg->num_inputs) {
			data->weights[id] = val;
			break;
		}

		LOG_ERR("%s: attempt to set unknown value #%d", dev->name, id);
		rc = -EINVAL;
	}

	return rc;
}

static const struct value_driver_api mix_api = {
	.get = mix_value_get,
	.set = mix_value_set,
};

static int mix_init(const struct device *dev)
{
	mix_weights_reset(dev);

#if IS_ENABLED(CONFIG_VALUE_MIX_SETTINGS)
	int rc;

	rc = mix_weights_load(dev);
	if (rc) {
		return rc;
	}

#endif /* IS_ENABLED(CONFIG_VALUE_MIX_SETTINGS) */

	return 0;
}

#define _MIX_VALUES(id)	\
	DT_INST_PROP_LEN(id, values)

#define _MIX_WEIGHT(node_id, prop, idx)					    \
	FIXP_CONST((double)(int32_t)DT_PROP_BY_IDX(node_id, weights, idx) / \
		   (double)DT_PROP(node_id, weight_divider),		    \
		   _MIX_WEIGHT_SCALE(node_id)),

#define _MIX_INPUT(node_id, prop, idx)					    \
	{								    \
		.value_spec = VALUE_DT_SPEC_GET_BY_IDX(node_id, prop, idx), \
		.default_weight = _MIX_WEIGHT(node_id, prop, idx)	    \
	},

#define _MIX_INPUT_SCALE(node_id, idx)				  \
	COND_CODE_1(DT_PROP_HAS_IDX(node_id, input_scales, idx),  \
		    (DT_PROP_BY_IDX(node_id, input_scales, idx)), \
		    (DT_PROP_OR(node_id, input_scale, 1)))

#define _MIX_WEIGHT_SCALE(node_id) \
	DT_PROP(node_id, weight_scale)

#define _MIX_OUTPUT_SCALE(node_id) \
	DT_PROP(node_id, output_scale)

#define _MIX_CALC_VALUE(node_id, prop, idx)			\
	rc = value_get_dt(&inputs[idx].value_spec, &val);	\
								\
	if (rc != 0) {						\
		goto end;					\
	}							\
								\
	if (weights[idx] != 0) {				\
		res += FIXP_MUL(val, weights[idx],		\
				_MIX_INPUT_SCALE(node_id, idx),	\
				_MIX_WEIGHT_SCALE(node_id),	\
				_MIX_OUTPUT_SCALE(node_id));	\
	}

#define MIX_DEVICE(id)						    \
	BUILD_ASSERT(DT_INST_PROP_LEN(id, weights) ==		    \
		     _MIX_VALUES(id),				    \
		     "Number of values and weights must be same");  \
								    \
	MIX_SETTINGS_HANDLER_DEFINE(id);			    \
								    \
	static int mix_calc_##id(const struct mix_input *inputs,    \
				 const value_t *weights,	    \
				 value_t *output)		    \
	{							    \
		value_t val;					    \
		value_t res = 0;				    \
		int rc;						    \
								    \
		DT_INST_FOREACH_PROP_ELEM(id, values,		    \
					  _MIX_CALC_VALUE);	    \
								    \
		*output = res;					    \
end:								    \
		return rc;					    \
	}							    \
								    \
	static MIX_DATA_STRUCT(, _MIX_VALUES(id)) mix_data_##id = { \
		.active = DT_INST_PROP(id, initial_active),	    \
	};							    \
								    \
	static const MIX_CONFIG_STRUCT(, _MIX_VALUES(id))	    \
	mix_config_##id = {					    \
		.num_inputs = DT_INST_PROP_LEN(id, values),	    \
		.calc = mix_calc_##id,				    \
		.inputs = {					    \
			DT_INST_FOREACH_PROP_ELEM(id, values,	    \
						  _MIX_INPUT)	    \
		},						    \
	};							    \
								    \
	DEVICE_DT_INST_DEFINE(id, mix_init, NULL, &mix_data_##id,   \
			      &mix_config_##id, POST_KERNEL,	    \
			      CONFIG_VALUE_MIX_INIT_PRIORITY,	    \
			      &mix_api);

DT_INST_FOREACH_STATUS_OKAY(MIX_DEVICE)
