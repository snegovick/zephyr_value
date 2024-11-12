/*
 * Copyright (c) 2023 MBT
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>
#include <zephyr/device.h>
#include <zephyr/drivers/value.h>
#include <zephyr/dt-bindings/value/calc.h>
#include <zephyr/fixed_point.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(calc, CONFIG_VALUE_CALC_LOG_LEVEL);

#define DT_DRV_COMPAT CALC_DT_COMPAT

#define MAX_FLAG_BYTES ((CONFIG_VALUE_CALC_MAX_RESULTS + 7) / 8)

#define CALC_DATA_STRUCT(type_name, num_results_) \
	struct type_name {			  \
		bool active;			  \
		uint8_t ready[MAX_FLAG_BYTES];	  \
		value_t results[num_results_];	  \
	}

CALC_DATA_STRUCT(calc_data, 0);

typedef void calc_func(const struct value_dt_spec *values,
		       uint8_t *ready,
		       value_t *results);

#define CALC_CONFIG_STRUCT(type_name, num_values_) \
	struct type_name {			   \
		uint8_t num_results;		   \
		uint8_t num_values;		   \
		calc_func *calculate;		   \
		const struct value_dt_spec	   \
			values[num_values_];	   \
	}

CALC_CONFIG_STRUCT(calc_config, 0);

static inline bool is_flag(const uint8_t *data, unsigned bit)
{
	return (data[bit / 8] >> (bit % 8)) & 1;
}

static inline void set_flag(uint8_t *data, unsigned bit)
{
	data[bit / 8] |= 1 << (bit % 8);
}

static inline void reset_flag(uint8_t *data, unsigned bit)
{
	data[bit / 8] &= ~(1 << (bit % 8));
}

static inline void reset_flags(uint8_t *data)
{
	memset(data, 0, MAX_FLAG_BYTES);
}

static void calc_task(const struct device *dev)
{
	const struct calc_config *cfg = dev->config;
	struct calc_data *data = dev->data;

	cfg->calculate(cfg->values, data->ready, data->results);
}

static int calc_value_get(const struct device *dev, value_id_t id, value_t *pval)
{
	const struct calc_config *cfg = dev->config;
	struct calc_data *data = dev->data;
	int rc = 0;

	switch (id) {
	case CALC_STATE:
		*pval = data->active;
		break;

	case CALC_RESULTS:
		*pval = cfg->num_results;
		break;

	default:
		if (id < cfg->num_results) {
			*pval = data->results[id];
			break;
		}

		LOG_ERR("%s: attempt to get unknown value #%u", dev->name, id);

		rc = -EINVAL;
	}

	return rc;
}

static int calc_value_set(const struct device *dev, value_id_t id, value_t val)
{
	// const struct calc_config *cfg = dev->config;
	struct calc_data *data = dev->data;
	int rc = 0;

	switch (id) {
	case CALC_STATE:
		if (data->active == val) {
			break;
		}

		data->active = val;
		reset_flags(data->ready);

		break;

	case CALC_SYNC:
		if (data->active) {
			calc_task(dev);
		}
		break;

	default:
		LOG_ERR("%s: attempt to set unknown value #%u", dev->name, id);

		rc = -EINVAL;
	}

	return rc;
}

static const struct value_driver_api calc_api = {
	.get = calc_value_get,
	.set = calc_value_set,
};

static int calc_init(const struct device *dev)
{
	return 0;
}

#define _CALC_OP_scl(a, b, sa, sb, sr) FIXP_RESCALE(a, sa, sr)
#define _CALC_OP_neg(a, b, sa, sb, sr) FIXP_NEG(a, sa, sr)
#define _CALC_OP_inv(a, b, sa, sb, sr) FIXP_INV(a, sa, sr)
#define _CALC_OP_add(a, b, sa, sb, sr) FIXP_ADD(a, b, sa, sb, sr)
#define _CALC_OP_sub(a, b, sa, sb, sr) FIXP_SUB(a, b, sa, sb, sr)
#define _CALC_OP_mul(a, b, sa, sb, sr) FIXP_MUL(a, b, sa, sb, sr)
#define _CALC_OP_div(a, b, sa, sb, sr) FIXP_DIV(a, b, sa, sb, sr)
#define _CALC_OP_min(a, b, sa, sb, sr) MIN(FIXP_RESCALE(a, sa, sr), FIXP_RESCALE(b, sb, sr))
#define _CALC_OP_max(a, b, sa, sb, sr) MAX(FIXP_RESCALE(a, sa, sr), FIXP_RESCALE(b, sb, sr))

#define _CALC_OP_IS_SAFE_scl(a, b) true
#define _CALC_OP_IS_SAFE_neg(a, b) true
#define _CALC_OP_IS_SAFE_inv(a, b) ((a) != 0)
#define _CALC_OP_IS_SAFE_add(a, b) true
#define _CALC_OP_IS_SAFE_sub(a, b) true
#define _CALC_OP_IS_SAFE_mul(a, b) true
#define _CALC_OP_IS_SAFE_div(a, b) ((b) != 0)
#define _CALC_OP_IS_SAFE_min(a, b) true
#define _CALC_OP_IS_SAFE_max(a, b) true

#define _CALC_VAR(node_id, prop) \
	UTIL_CAT(_var_, DT_STRING_TOKEN(node_id, prop))

#define _CALC_VAR_N(node_id, prop, idx)	\
	UTIL_CAT(_var_, DT_STRING_TOKEN_BY_IDX(node_id, value_names, idx))

#define _CALC_ARG_VAR(n, p) UTIL_CAT(UTIL_CAT(arg, n), UTIL_CAT(_, p))

#define _CALC_ARG(node_id, n)					   \
	COND_CODE_1(DT_NODE_HAS_PROP(node_id, UTIL_CAT(arg, n)),   \
		    (FIXP_DT_PROP_SCALE(node_id, UTIL_CAT(arg, n), \
					_CALC_ARG_VAR(n, scale))), \
		    (_CALC_VAR(node_id, _CALC_ARG_VAR(n, name))))

#define _CALC_ARG_IS_READY(node_id, n)				 \
	COND_CODE_1(DT_NODE_HAS_PROP(node_id,			 \
				     _CALC_ARG_VAR(n, name)),	 \
		    (UTIL_CAT(_CALC_VAR(node_id,		 \
					_CALC_ARG_VAR(n, name)), \
			      _ready)), (true))

#define _CALC_ARG_SCALE(node_id, n)					 \
	COND_CODE_1(DT_NODE_HAS_PROP(node_id, _CALC_ARG_VAR(n, name)),	 \
		    (UTIL_CAT(_scl_,					 \
			      DT_STRING_TOKEN(node_id,			 \
					      _CALC_ARG_VAR(n, name)))), \
		    (DT_PROP(node_id, _CALC_ARG_VAR(n, scale))))

#define _CALC_RES_DEF(node_id)						  \
	IF_ENABLED(DT_NODE_HAS_PROP(node_id, res_name),			  \
		   (value_t _CALC_VAR(node_id, res_name);		  \
		    bool UTIL_CAT(_CALC_VAR(node_id, res_name), _ready);  \
		    const value_t					  \
		    UTIL_CAT(_scl_, DT_STRING_TOKEN(node_id, res_name)) = \
			    DT_PROP(node_id, res_scale); ))

#define _CALC_RES(node_id)				\
	IF_ENABLED(DT_NODE_HAS_PROP(node_id, res_name),	\
		   (_CALC_VAR(node_id, res_name) = ))	\
	IF_ENABLED(DT_NODE_HAS_PROP(node_id, res_id),	\
		   (results[DT_PROP(node_id, res_id)] = ))

#define _CALC_RES_SCALE(node_id) \
	DT_PROP(node_id, res_scale)

#define _CALC_RES_SET_READY(node_id, state)			     \
	IF_ENABLED(DT_NODE_HAS_PROP(node_id, res_name),		     \
		   (UTIL_CAT(_CALC_VAR(node_id, res_name), _ready) = \
			    COND_CODE_1(state, (true), (false)); ))  \
	IF_ENABLED(DT_NODE_HAS_PROP(node_id, res_id),		     \
		   (COND_CODE_1(state, (set_flag), (reset_flag))     \
		    (ready, DT_PROP(node_id, res_id)); ))

#define _CALC_VALUE_DEF(node_id, prop, idx)			       \
	value_t _CALC_VAR_N(node_id, value_names, idx);		       \
	bool UTIL_CAT(_CALC_VAR_N(node_id, value_names, idx), _ready); \
	const value_t						       \
	UTIL_CAT(_scl_, DT_STRING_TOKEN_BY_IDX(node_id,		       \
					       value_names, idx)) =    \
		DT_PROP_BY_IDX(node_id, value_scales, idx);

#define _CALC_VALUE_GET(node_id, prop, idx)			   \
	UTIL_CAT(_CALC_VAR_N(node_id, value_names, idx), _ready) = \
	0 ==							   \
	value_get_dt(&values[idx],				   \
		     &_CALC_VAR_N(node_id, value_names, idx));

#define _CALC_OP_IS_SAFE(node_id) \
	UTIL_CAT(_CALC_OP_IS_SAFE_, DT_STRING_TOKEN(node_id, op))

#define _CALC_OP(node_id) \
	UTIL_CAT(_CALC_OP_, DT_STRING_TOKEN(node_id, op))

#define _CALC_OP_IMPL(node_id)					\
	if (_CALC_ARG_IS_READY(node_id, 1) &&			\
	    _CALC_ARG_IS_READY(node_id, 2) &&			\
	    _CALC_OP_IS_SAFE(node_id)(_CALC_ARG(node_id, 1),	\
				      _CALC_ARG(node_id, 2))) {	\
		_CALC_RES(node_id)				\
		_CALC_OP(node_id)(_CALC_ARG(node_id, 1),	\
				  _CALC_ARG(node_id, 2),	\
				  _CALC_ARG_SCALE(node_id, 1),	\
				  _CALC_ARG_SCALE(node_id, 2),	\
				  _CALC_RES_SCALE(node_id));	\
		_CALC_RES_SET_READY(node_id, 1)			\
	} else {						\
		_CALC_RES_SET_READY(node_id, 0)			\
	}

#define _CALC_VALUE_SPEC(node_id, prop, idx) \
	VALUE_DT_SPEC_GET_BY_IDX(node_id, prop, idx),

#define _CALC_RES_INIT(node_id)			      \
	IF_ENABLED(DT_NODE_HAS_PROP(node_id, res_id), \
		   ([DT_PROP(node_id, res_id)] = 0, ))

#define _CALC_RES_ID(node_id, id)			 \
	IF_ENABLED(DT_NODE_HAS_PROP(node_id, res_id),	 \
		   (UTIL_CAT(calc_res_id_##id##_,	 \
			     DT_NODE_CHILD_IDX(node_id)) \
			    = DT_PROP(node_id, res_id), ))

#define _CALC_NUM_RESULTS(id) calc_res_num_##id

#define _CALC_NUM_VALUES(id) DT_INST_PROP_LEN(id, values)

#define CALC_DEVICE(id)							\
									\
	enum calc_res_ids_##id {					\
		DT_INST_FOREACH_CHILD_VARGS(id, _CALC_RES_ID, id)	\
		calc_res_num_##id,					\
	};								\
									\
	static void calc_func_##id(const struct value_dt_spec *values,	\
				   uint8_t *ready,			\
				   value_t *results)			\
	{								\
		DT_INST_FOREACH_PROP_ELEM(id, values, _CALC_VALUE_DEF);	\
		DT_INST_FOREACH_CHILD(id, _CALC_RES_DEF);		\
									\
		DT_INST_FOREACH_PROP_ELEM(id, values, _CALC_VALUE_GET);	\
		DT_INST_FOREACH_CHILD(id, _CALC_OP_IMPL);		\
	}								\
									\
	static CALC_DATA_STRUCT(, _CALC_NUM_RESULTS(id))		\
	calc_data_##id = {						\
		.active = DT_INST_PROP(id, initial_active),		\
	};								\
									\
	static const CALC_CONFIG_STRUCT(, _CALC_NUM_VALUES(id))		\
	calc_config_##id = {						\
		.num_results = _CALC_NUM_RESULTS(id),			\
		.num_values = _CALC_NUM_VALUES(id),			\
		.calculate = calc_func_##id,				\
		.values = {						\
			DT_INST_FOREACH_PROP_ELEM(id, values,		\
						  _CALC_VALUE_SPEC)	\
		},							\
	};								\
									\
	DEVICE_DT_INST_DEFINE(id, calc_init, NULL,			\
			      &calc_data_##id,				\
			      &calc_config_##id, POST_KERNEL,		\
			      CONFIG_VALUE_CALC_INIT_PRIORITY,		\
			      &calc_api);

DT_INST_FOREACH_STATUS_OKAY(CALC_DEVICE)
