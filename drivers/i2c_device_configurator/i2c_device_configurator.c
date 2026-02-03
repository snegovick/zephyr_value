#include <zephyr/dt-bindings/i2c_device_configurator.h>
#include <zephyr/drivers/value.h>
#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/logging/log.h>

#define DT_DRV_COMPAT I2C_DEVICE_CONFIGURATOR_DT_COMPAT

LOG_MODULE_REGISTER(i2c_device_configurator, CONFIG_I2C_DEVICE_CONFIGURATOR_LOG_LEVEL);

/* operation code */
enum cw_op {
	/* end of operations */
	/* [OP] */
	op_done         = 0,
	/* write data as is */
	/* [OP, LEN, DATA...] */
	op_write        = 1,
};

typedef uint8_t op_t;

#define OP_BITS 8
#define OP_CODE_BITS 1
#define OP_SIZE_BITS (OP_BITS - OP_CODE_BITS)
#define OP_SIZE_MASK (~(~0 << OP_SIZE_BITS))
#define OP_SIZE_MAX OP_SIZE_MASK

#define OP_PACK(code, size) (((code) << OP_SIZE_BITS) | (size))
#define OP_CODE(op) ((op) >> OP_SIZE_BITS)
#define OP_SIZE(op) ((op) & OP_SIZE_MASK)

struct i2c_device_configurator_data {
	/* pointer to device (needed for delayable work and alert handler) */
	const struct device *dev;

	/* delayed work */
	struct k_work_delayable work;

	/* value subscriptions list */
	struct value_sub sub;

	/* error code or 0 */
	uint8_t status;

	/* current stage */
	uint8_t state;
};

struct i2c_device_configurator_state_ops {
	/* state identifier */
	uint8_t state;
	/* operation sequence */
	uint8_t ops[];
};

struct i2c_device_configurator_config {
	/* i2c bus device */
	const struct i2c_dt_spec i2c_spec;

	/* number of states with operations */
	uint8_t num_state_ops;

	/* array of states with operations */
	const struct i2c_device_configurator_state_ops *state_ops[];
};

static int cw_write(const struct i2c_device_configurator_config *cfg, const uint8_t data[], size_t size)
{
	// LOG_DBG("Write %i bytes to addr 0x%x", size, cfg->i2c_addr);

	int rc = i2c_write_dt(&cfg->i2c_spec, data, size);

	if (rc < 0) {
		LOG_ERR("Error while writing bytes to the device");
	}

	return rc;
}

static int cw_exec(const struct i2c_device_configurator_config *cfg, const uint8_t ops[])
{
	const uint8_t *op = ops;
	uint8_t size;

	for (; ;) {
		switch (OP_CODE(op[0])) {
		case op_write:
			size = OP_SIZE(op[0]);
			if (cw_write(cfg, &op[1], size)) {
				return EFAULT;
			}
			op += 1 + size;
			break;
		case op_done:
			return 0;
		}
	}
}

static void cw_task(struct i2c_device_configurator_data *data, bool notify)
{
	const struct device *dev = data->dev;
	const struct i2c_device_configurator_config *cfg = dev->config;
	const struct i2c_device_configurator_state_ops *const
	*state_ops = cfg->state_ops;
	const struct i2c_device_configurator_state_ops *const
	*end_state_ops = cfg->state_ops + cfg->num_state_ops;

	for (; state_ops < end_state_ops; state_ops++) {
		if ((*state_ops)->state != data->state) {
			continue;
		}

		if (cw_exec(cfg, (*state_ops)->ops)) {
			data->status = EFAULT;
			break;
		}
	}

	if (data->status == EAGAIN) {
		data->status = 0;
	}

	if (notify) {
		value_sub_notify(&data->sub, dev, I2C_DEVICE_CONFIGURATOR_STATE);
	}
}

static void cw_work(struct k_work *work)
{
	struct i2c_device_configurator_data *data = CONTAINER_OF(work,
								 struct i2c_device_configurator_data,
								 work);

	cw_task(data, true);
}

static int cw_value_get(const struct device *dev, value_id_t id, value_t *pval)
{
	struct i2c_device_configurator_data *data = dev->data;
	int status = 0;

	switch (id) {

	case I2C_DEVICE_CONFIGURATOR_STATE:
		*pval = data->state;
		status = -(int)data->status;
		break;

	default:
		LOG_ERR("%s: attempt to get unknown value #%d", dev->name, id);
		return -EINVAL;
	}

	return status;
}

static int cw_value_set(const struct device *dev, value_id_t id, value_t val)
{
	struct i2c_device_configurator_data *data = dev->data;
	int status = 0;

	switch (id) {

	case I2C_DEVICE_CONFIGURATOR_STATE:
		LOG_DBG("set %u <- %u (%i)", val, data->state, data->status);

		/* force change state anyway when failed */
		if (data->status != EFAULT) {
			if (data->state == val) {
				/* already in desired state */
				break;
			}

			if (data->status == EAGAIN) {
				/* pending operation */
				status = -EAGAIN;
				break;
			}
		}

		data->state = val;

		data->status = EAGAIN;
		k_work_schedule(&data->work, K_NO_WAIT);

		break;

	default:
		LOG_ERR("%s: attempt to set unknown value #%d", dev->name, id);
		return -EINVAL;
	}

	return status;
}

static int cw_value_sub(const struct device *dev,
			value_id_t id,
			struct value_sub_cb *cb,
			bool sub)
{
	struct  i2c_device_configurator_data *data = dev->data;

	switch (id) {
	case I2C_DEVICE_CONFIGURATOR_STATE:
		value_sub_manage(&data->sub, cb, sub);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static const struct value_driver_api i2c_device_configurator_api = {
	.get = cw_value_get,
	.set = cw_value_set,
	.sub = cw_value_sub,
};

static int i2c_device_configurator_init(const struct device *dev)
{
	cw_task(dev->data, false);

	return 0;
}

#define BYTES_PROP_HAS(node_id, prop)		 \
	UTIL_OR(DT_NODE_HAS_PROP(node_id, prop), \
		DT_NODE_HAS_PROP(node_id, prop##_raw))

#define BYTES_PROP_LEN(node_id, prop)		     \
	COND_CODE_1(DT_NODE_HAS_PROP(node_id, prop), \
		    (DT_PROP_LEN(node_id, prop)),    \
		    (DT_PROP_LEN(node_id, prop##_raw)))

#define _BYTES_PROP_VAL(node_id, prop, idx) \
	DT_PROP_BY_IDX(node_id, prop, idx),

#define _BYTES_PROP_VALS(node_id, prop)	\
	DT_FOREACH_PROP_ELEM(node_id, prop, _BYTES_PROP_VAL)

#define BYTES_PROP_VAL(node_id, prop)		       \
	COND_CODE_1(DT_NODE_HAS_PROP(node_id, prop),   \
		    (_BYTES_PROP_VALS(node_id, prop)), \
		    (_BYTES_PROP_VALS(node_id, prop##_raw)))

#define PLUS_ONE(...) + 1

#define OP_LENGTH(node_id)				     \
	(0 IF_ENABLED(BYTES_PROP_HAS(node_id, cmd),	     \
		      (+BYTES_PROP_LEN(node_id, cmd)))	     \
	 IF_ENABLED(UTIL_AND(DT_PROP(node_id, block),	     \
			     BYTES_PROP_HAS(node_id, data)), \
		    (+1))				     \
	 IF_ENABLED(BYTES_PROP_HAS(node_id, data),	     \
		    (+BYTES_PROP_LEN(node_id, data))))

#define OP_CHECK(node_id)			       \
	BUILD_ASSERT(OP_LENGTH(node_id) < OP_SIZE_MAX, \
		     "Operation data length too long");

#define OP_ENCODE(node_id)				    \
	/* push op code and size */			    \
	OP_PACK(op_write, OP_LENGTH(node_id)),		    \
	/* push cmd bytes */				    \
	IF_ENABLED(BYTES_PROP_HAS(node_id, cmd),	    \
		   (BYTES_PROP_VAL(node_id, cmd)))	    \
	/* push length byte for block write */		    \
	IF_ENABLED(UTIL_AND(DT_PROP(node_id, block),	    \
			    BYTES_PROP_HAS(node_id, data)), \
		   (BYTES_PROP_LEN(node_id, data), ))	    \
	/* push data bytes */				    \
	IF_ENABLED(BYTES_PROP_HAS(node_id, data),	    \
		   (BYTES_PROP_VAL(node_id, data)))

#define STATE_CONFIG_NAME(node_id, id) i2c_device_configurator_state_ops_##id##_##node_id

#define STATE_CONFIG_REF(node_id, id) & STATE_CONFIG_NAME(node_id, id),

#define STATE_CONFIG_IMPL(node_id, id)			      \
							      \
	DT_FOREACH_CHILD(node_id, OP_CHECK);		      \
							      \
	static const struct i2c_device_configurator_state_ops \
	STATE_CONFIG_NAME(node_id, id) = {		      \
		.state = DT_PROP(node_id, state),	      \
		.ops = {				      \
			DT_FOREACH_CHILD(node_id, OP_ENCODE)  \
			op_done },			      \
	};

#define I2C_DEVICE_CONFIGURATOR_DEVICE(id)					 \
										 \
	static struct i2c_device_configurator_data				 \
		i2c_device_configurator_data_##id = {				 \
		.dev = DEVICE_DT_GET(DT_DRV_INST(id)),				 \
		.work = Z_WORK_DELAYABLE_INITIALIZER(cw_work),			 \
		.sub = VALUE_SUB_INIT(),					 \
		.status = 0,							 \
		.state = DT_INST_PROP(id, initial_state),			 \
	};									 \
										 \
	DT_INST_FOREACH_CHILD_VARGS(id, STATE_CONFIG_IMPL, id);			 \
										 \
	static const struct i2c_device_configurator_config			 \
		i2c_device_configurator_config_##id = {				 \
		.i2c_spec = I2C_DT_SPEC_INST_GET(id),				 \
		.num_state_ops = 0 DT_INST_FOREACH_CHILD(id, PLUS_ONE),		 \
		.state_ops = {							 \
			DT_INST_FOREACH_CHILD_VARGS(id, STATE_CONFIG_REF, id)	 \
		},								 \
	};									 \
										 \
	DEVICE_DT_INST_DEFINE(id, i2c_device_configurator_init, NULL,		 \
			      &i2c_device_configurator_data_##id,		 \
			      &i2c_device_configurator_config_##id, POST_KERNEL, \
			      CONFIG_I2C_DEVICE_CONFIGURATOR_INIT_PRIORITY,	 \
			      &i2c_device_configurator_api);

DT_INST_FOREACH_STATUS_OKAY(I2C_DEVICE_CONFIGURATOR_DEVICE)
