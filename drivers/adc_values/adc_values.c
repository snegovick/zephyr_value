/*
 * Copyright (c) 2023 MBT
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/dt-bindings/value/adc.h>
#include <zephyr/drivers/value.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/logging/log.h>

#define DT_DRV_COMPAT ADC_VALUES_DT_COMPAT

LOG_MODULE_REGISTER(adc_values, CONFIG_ADC_VALUES_LOG_LEVEL);

#define ADC_VALUES_FLAG_BYTES ((CONFIG_ADC_VALUES_MAX_CHANNELS + 7) / 8)

struct adc_values_data {
	bool active;
	uint8_t channel;
	struct adc_sequence sequence;
	struct k_work work;
	uint8_t ready[ADC_VALUES_FLAG_BYTES];
	uint8_t fault[ADC_VALUES_FLAG_BYTES];
	value_t values[];
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
	memset(data, 0, ADC_VALUES_FLAG_BYTES);
}

struct adc_values_config {
	const struct adc_dt_spec *channel_specs;
	value_t (*convert)(value_id_t id, uint16_t raw);
	uint8_t num_channels;
};

static void adc_values_task(const struct device *dev, bool cont)
{
	const struct adc_values_config *cfg = dev->config;
	struct adc_values_data *data = dev->data;
	unsigned count;
	int rc;

	for (count = 0; count < cfg->num_channels; count++) {
		if (cont) {
			// convert sample to value
			data->values[data->channel] =
				cfg->convert(data->channel, *(uint16_t *)data->sequence.buffer);

			set_flag(data->ready, data->channel);

			if (data->channel < cfg->num_channels - 1) {
				// select next channel
				data->channel++;
			} else {
				break;
			}
		} else {
			data->channel = 0;
		}

		// configure channel
		rc = adc_channel_setup_dt(&cfg->channel_specs[data->channel]);
		if (rc) {
			LOG_ERR("%s: Error when setup ADC channel: #%u",
				dev->name, data->channel);
			goto err;
		}

		// configure sequence
		rc = adc_sequence_init_dt(&cfg->channel_specs[data->channel],
					  &data->sequence);
		if (rc) {
			LOG_ERR("%s: Error when init ADC sequence: #%u",
				dev->name, data->channel);
			goto err;
		}

		// start conversion
		rc = adc_read_async(cfg->channel_specs[data->channel].dev,
				    &data->sequence, NULL);
		if (rc) {
			LOG_ERR("%s: Error when start conversion: #%u",
				dev->name, data->channel);
			goto err;
		}

		break;

err:
		set_flag(data->fault, data->channel);
		continue;
	}
}

static void adc_values_work_handler(struct k_work *work)
{
	struct adc_values_data *data = CONTAINER_OF(work, struct adc_values_data, work);
	const struct device *dev = data->sequence.options->user_data;

	adc_values_task(dev, true);
}

static enum adc_action adc_values_sequence_callback(const struct device *adc_dev,
						    const struct adc_sequence *sequence,
						    uint16_t sampling_index)
{
	const struct device *dev = sequence->options->user_data;
	struct adc_values_data *data = dev->data;

	k_work_submit(&data->work);

	return ADC_ACTION_FINISH;
}

static int adc_values_value_get(const struct device *dev, value_id_t id, value_t *pval)
{
	const struct adc_values_config *cfg = dev->config;
	struct adc_values_data *data = dev->data;
	unsigned chn;
	int rc = 0;

	switch (id) {
	case ADC_VALUES_STATE:
		*pval = data->active;
		break;

	case ADC_VALUES_NUM_CHANNELS:
		*pval = cfg->num_channels;
		break;

	default:
		if (id & ADC_VALUES_CHANNEL_FLAG) {
			chn = ADC_VALUES_CHANNEL_GET(id);
			if (chn < cfg->num_channels) {
				*pval = data->values[chn];
				if (is_flag(data->fault, chn)) {
					rc = -EFAULT;
				} else if (!is_flag(data->ready, chn)) {
					rc = -EAGAIN;
				}
				break;
			}
		}

		LOG_ERR("%s: attempt to get unknown value #%d", dev->name, id);
		rc = -EINVAL;
	}

	return rc;
}

static int adc_values_value_set(const struct device *dev, value_id_t id, value_t val)
{
	struct adc_values_data *data = dev->data;
	int rc = 0;

	switch (id) {
	case ADC_VALUES_STATE:
		if (data->active && !val) {
			reset_flags(data->ready);
			reset_flags(data->fault);
		}

		data->active = val;

		break;

	case ADC_VALUES_SYNC:
		if (data->active) {
			adc_values_task(dev, false);
		}
		break;

	default:
		LOG_ERR("%s: attempt to set unknown value #%d", dev->name, id);
		rc = -EINVAL;
	}

	return rc;
}

static const struct value_driver_api adc_values_api = {
	.get = adc_values_value_get,
	.set = adc_values_value_set,
};

static int adc_values_init(const struct device *dev)
{
	const struct adc_values_config *cfg = dev->config;
	unsigned i;
	int rc;

	// Try to configure all known ADC channels
	for (i = 0; i < cfg->num_channels; i++) {
		rc = adc_channel_setup_dt(&cfg->channel_specs[i]);
		if (rc) {
			LOG_ERR("%s: Error when setup ADC channel: #%u", dev->name, i);
		}
	}

	return 0;
}

#define NUM_CONS(numerator, denominator) \
	(((double)(numerator)) / ((double)(denominator)))

#define DT_PROP_BY_IDX_OR(node_id, prop, idx, default_value) \
	COND_CODE_1(DT_PROP_HAS_IDX(node_id, prop, idx),     \
		    (DT_PROP_BY_IDX(node_id, prop, idx)),    \
		    (default_value))

#define DT_PROP_NUM_OR(node_id, prop, default_value)			\
	COND_CODE_1(DT_PROP_HAS_IDX(node_id, prop, 0),			\
		    (NUM_CONS(DT_PROP_BY_IDX(node_id, prop, 0),		\
			      DT_PROP_BY_IDX_OR(node_id, prop, 1, 1))),	\
		    (default_value))

#define DT_PROP_NUM_OR_SCALED(node_id, prop, default_value, scale_prop)	\
	(DT_PROP_NUM_OR(node_id, prop, default_value) *			\
	 DT_PROP_OR(node_id, scale_prop, 1))

#define SAMPLE_CONVERT(raw, gain, bias)	\
	(((value_t)(raw)) * ((value_t)(gain)) + ((value_t)(bias)))

#define _DT_GET_GAIN(node_id)					      \
	COND_CODE_1(UTIL_AND(DT_NODE_HAS_PROP(node_id, vref),	      \
			     DT_NODE_HAS_PROP(node_id, sat)),	      \
		    (DT_PROP_NUM_OR_SCALED(node_id, gain, 1, scale) * \
		     DT_PROP_NUM_OR(node_id, vref, 1) /		      \
		     DT_PROP(node_id, sat)),			      \
		    (DT_PROP_NUM_OR_SCALED(node_id, gain, 1, scale)))

#define _DT_GET_BIAS(node_id) \
	DT_PROP_NUM_OR_SCALED(node_id, bias, 0, scale)

#define _DT_CHANNEL_CONVERT(node_id)		     \
case DT_REG_ADDR(node_id):			     \
	return SAMPLE_CONVERT(raw,		     \
			      _DT_GET_GAIN(node_id), \
			      _DT_GET_BIAS(node_id));

#define _DT_CHANNEL_INIT(node_id) \
	0,

#define _DT_CHANNEL_SPEC(node_id) \
	[DT_REG_ADDR(node_id)] = ADC_DT_SPEC_GET_BY_IDX(node_id, 0),

#define ADC_VALUES_DEVICE(inst)						     \
									     \
	value_t adc_values_convert_##inst(value_id_t id, uint16_t raw)	     \
	{								     \
		switch (id) {						     \
			DT_INST_FOREACH_CHILD(inst, _DT_CHANNEL_CONVERT)     \
		}							     \
		return 0;						     \
	}								     \
									     \
	static const struct adc_dt_spec adc_values_channels_##inst[] = {     \
		DT_INST_FOREACH_CHILD(inst, _DT_CHANNEL_SPEC)		     \
	};								     \
									     \
	static const struct adc_sequence_options			     \
		adc_values_sequence_options_##inst = {			     \
		.interval_us = 0,       /* TODO: make it configurable */     \
		.callback = adc_values_sequence_callback,		     \
		.user_data = (void *)DEVICE_DT_INST_GET(inst),		     \
	};								     \
									     \
	uint16_t adc_values_samples_buffer_##inst[1];			     \
									     \
	static struct adc_values_data adc_values_data_##inst = {	     \
		.active = DT_INST_PROP(inst, initial_active),		     \
		.work = Z_WORK_INITIALIZER(adc_values_work_handler),	     \
		.sequence = {						     \
			.options = &adc_values_sequence_options_##inst,	     \
			.buffer = adc_values_samples_buffer_##inst,	     \
			.buffer_size =					     \
				sizeof(adc_values_samples_buffer_##inst),    \
		},							     \
		.values = { DT_INST_FOREACH_CHILD(inst, _DT_CHANNEL_INIT) }, \
	};								     \
									     \
	static const struct adc_values_config adc_values_config_##inst = {   \
		.channel_specs = adc_values_channels_##inst,		     \
		.num_channels = ARRAY_SIZE(adc_values_channels_##inst),	     \
		.convert = adc_values_convert_##inst,			     \
	};								     \
									     \
	DEVICE_DT_INST_DEFINE(inst, adc_values_init, NULL,		     \
			      &adc_values_data_##inst,			     \
			      &adc_values_config_##inst, POST_KERNEL,	     \
			      CONFIG_ADC_VALUES_INIT_PRIORITY,		     \
			      &adc_values_api);

DT_INST_FOREACH_STATUS_OKAY(ADC_VALUES_DEVICE)
