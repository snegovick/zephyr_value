#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/value.h>
#include <zephyr/dt-bindings/gpio_extra.h>
#include <zephyr/dt-bindings/regulator_extended.h>
#include <zephyr/drivers/gpio.h>

#define DT_DRV_COMPAT REGEXT_DT_COMPAT

#include <zephyr/logging/log_ctrl.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(regulator_extended, CONFIG_REGULATOR_EXTENDED_LOG_LEVEL);

#define OPTION_ALWAYS_ON BIT(0)
#define OPTION_BOOT_ON BIT(1)

enum driver_state {
	driver_state_idle = 0,
	driver_state_defferred,
	driver_state_pending,
	driver_state_failed,
};

#if IS_ENABLED(CONFIG_REGULATOR_EXTENDED_MONITOR_PGOODS)

typedef uint32_t pgood_gpio_dt_flags_t;

struct pgood_gpio_dt_spec {
	/** GPIO device controlling the pin */
	const struct device *port;
	/** The pin's number on the device */
	gpio_pin_t pin;
	/** The pin's configuration flags as specified in devicetree */
	pgood_gpio_dt_flags_t dt_flags;
};

#else /* IS_ENABLED(CONFIG_REGULATOR_EXTENDED_MONITOR_PGOODS) */

#define pgood_gpio_dt_flags_t gpio_dt_flags_t
#define pgood_gpio_dt_spec gpio_dt_spec

#endif /* IS_ENABLED(CONFIG_REGULATOR_EXTENDED_MONITOR_PGOODS) */

struct driver_config {
	const struct gpio_dt_spec *enable_gpio;
	const struct pgood_gpio_dt_spec *pgood_gpio;
	uint32_t startup_delay_us;
	uint32_t off_on_delay_us;
	uint8_t num_enables;
	uint8_t num_pgoods;
	uint8_t options;
};

struct driver_data {
	/* pointer to device (needed for work queue) */
	const struct device *dev;
	/* work for delay wait */
	struct k_work_delayable work;
	/* value subscriptions list */
	struct value_sub sub;
	/* turning state */
	enum driver_state state;
	/* regulator status */
	bool enabled;
#if IS_ENABLED(CONFIG_REGULATOR_EXTENDED_MONITOR_PGOODS)
	/* marker to find start of callbacks */
	//gpio_port_pins_t marker;
	struct gpio_callback marker;
	/* pgood callbacks */
	struct gpio_callback pgood_cb[];
#endif /* IS_ENABLED(CONFIG_REGULATOR_EXTENDED_MONITOR_PGOODS) */
};

#if IS_ENABLED(CONFIG_REGULATOR_EXTENDED_MONITOR_PGOODS)

/* check marker field alignment (for case when structure layout has changed) */
BUILD_ASSERT(offsetof(struct driver_data, marker) ==
	     offsetof(struct driver_data, pgood_cb[-1]),
	     "Invalid marker field alignment");

#endif /* IS_ENABLED(CONFIG_REGULATOR_EXTENDED_MONITOR_PGOODS) */

static int init_enables(const struct device *dev)
{
	const struct driver_config *cfg = dev->config;
	gpio_flags_t flags = cfg->options & (OPTION_ALWAYS_ON | OPTION_BOOT_ON) ?
			     GPIO_OUTPUT_ACTIVE : GPIO_OUTPUT_INACTIVE;
	int i;

	for (i = 0; i < cfg->num_enables; i++) {
		int rc = gpio_pin_configure_dt(&cfg->enable_gpio[i], flags);

		if (rc != 0) {
			LOG_ERR("%s: error when configure enable gpio #%u: %i",
				dev->name, i, rc);
			return rc;
		}
	}

	return 0;
}

static int set_enables(const struct device *dev, bool value)
{
	const struct driver_config *cfg = dev->config;
	unsigned i;
	int rc;

	for (i = 0; i < cfg->num_enables; i++) {
		rc = gpio_pin_set_dt(&cfg->enable_gpio[i], value);

		if (rc != 0) {
			LOG_ERR("%s: error when set enable gpio #%u: %i",
				dev->name, i, rc);
			return rc;
		}
	}

	return 0;
}

static bool has_pgoods(const struct device *dev)
{
	const struct driver_config *cfg = dev->config;

	return cfg->num_pgoods > 0;
}

static int test_pgoods(const struct device *dev, bool expected_state)
{
	const struct driver_config *cfg = dev->config;
	unsigned i;

	for (i = 0; i < cfg->num_pgoods; i++) {
		bool actual_state = gpio_pin_get_dt((const struct gpio_dt_spec *)
						    &cfg->pgood_gpio[i]);

		if (actual_state != expected_state) {
			return -EINVAL;
		}
	}

	return 0;
}

#if IS_ENABLED(CONFIG_REGULATOR_EXTENDED_MONITOR_PGOODS)

const struct device *device_from_pgood_cb(struct gpio_callback *cb)
{
	/* find start of pgood callbacks */
	for (; cb->pin_mask != 0; cb--);

	return CONTAINER_OF(cb, struct driver_data, marker)->dev;
}

static void handle_pgoods(const struct device *port, struct gpio_callback *cb,
			  gpio_port_pins_t pins)
{
	const struct device *dev = device_from_pgood_cb(cb);
	struct driver_data *data = dev->data;

	/* we need an extra test to prevent false-positive triggering */
	if (!test_pgoods(dev, data->enabled)) {
		return;
	}

	// data->enabled = !data->enabled;
	LOG_WRN("%s: test pgoods failed", dev->name);
	data->state = driver_state_failed;

	/* notify subscribers */
	value_sub_notify(&data->sub, dev, REGEXT_STATE);
}

static void async_pgoods(const struct device *dev, bool on)
{
	const struct driver_config *cfg = dev->config;
	struct driver_data *data = dev->data;
	const struct pgood_gpio_dt_spec *gpio;
	unsigned i;

	if (cfg->num_pgoods > 0) {
		LOG_DBG("%s %s pgood callbacks", dev->name, on ? "on" : "off");

		for (i = 0; i < cfg->num_pgoods; i++) {
			gpio = &cfg->pgood_gpio[i];

			if (on) {
				if (gpio->dt_flags & (data->enabled ?
						      GPIO_EDGE_TO_INACTIVE :
						      GPIO_EDGE_TO_ACTIVE)) {
					gpio_add_callback(gpio->port,
							  &data->pgood_cb[i]);
				}
			} else {
				gpio_remove_callback(cfg->pgood_gpio[i].port,
						     &data->pgood_cb[i]);
			}
		}
	}
}

#endif /* IS_ENABLED(CONFIG_REGULATOR_EXTENDED_MONITOR_PGOODS) */

static int init_pgoods(const struct device *dev)
{
	const struct driver_config *cfg = dev->config;
	struct driver_data *data = dev->data;
	const struct pgood_gpio_dt_spec *gpio;
	unsigned i;
	int rc;

	if (cfg->num_pgoods > 0) {
		LOG_DBG("%s configure pgood inputs", dev->name);

		for (i = 0; i < cfg->num_pgoods; i++) {
			gpio = &cfg->pgood_gpio[i];

#if IS_ENABLED(CONFIG_REGULATOR_EXTENDED_MONITOR_PGOODS)
			/* check if any interrupt is enabled */
			if (gpio->dt_flags & (GPIO_EDGE_TO_INACTIVE | GPIO_EDGE_TO_ACTIVE)) {
				gpio_init_callback(&data->pgood_cb[i], handle_pgoods, 1 << gpio->pin);

				rc = gpio_pin_interrupt_configure(gpio->port, gpio->pin,
								  GPIO_INT_LEVELS_LOGICAL |
								  (gpio->dt_flags & GPIO_EDGE_TO_INACTIVE ?
								   GPIO_INT_EDGE_FALLING : 0) |
								  (gpio->dt_flags & GPIO_EDGE_TO_ACTIVE ?
								   GPIO_INT_EDGE_RISING : 0));

				if (rc != 0) {
					LOG_DBG("%s: error while configuring pgood gpio interrupt #%u: %i",
						dev->name, i, rc);
					return rc;
				}
			}
#endif /* IS_ENABLED(CONFIG_REGULATOR_EXTENDED_MONITOR_PGOODS) */

			rc = gpio_pin_configure(gpio->port, gpio->pin,
						((gpio_dt_flags_t)gpio->dt_flags) | GPIO_INPUT);

			if (rc != 0) {
				LOG_ERR("%s: error when configure pgood gpio #%u: %i",
					dev->name, i, rc);
				return rc;
			}
		}
	}

#if IS_ENABLED(CONFIG_REGULATOR_EXTENDED_MONITOR_PGOODS)
	/* enable pgood callbacks */
	async_pgoods(dev, true);
#endif /* IS_ENABLED(CONFIG_REGULATOR_EXTENDED_MONITOR_PGOODS) */

	return 0;
}

static void finalize_transition(struct driver_data *data, uint32_t delay_us)
{
	const struct device *dev = data->dev;

	LOG_DBG("%s: finalize with delay %u us", dev->name, delay_us);

	/* If there's no error and we have to delay, do so. */
	if (delay_us > 0) {
		data->state = driver_state_pending;
		/* If the delay is less than a tick or we're not
		 * sleep-capable we have to busy-wait.
		 */
		if ((k_us_to_ticks_floor32(delay_us) == 0) || k_is_pre_kernel()) {
			LOG_DBG("busy wait");
			k_busy_wait(delay_us);
		} else {
			int rc;

			LOG_DBG("work queue wait");
			rc = k_work_schedule(&data->work, K_USEC(delay_us));

			if (rc >= 0) {
				LOG_DBG("schedule ok");
				return;
			}

			LOG_ERR("schedule failed: %i", rc);
			data->state = driver_state_failed;
		}
	}

	if (data->state != driver_state_failed && 0 != test_pgoods(dev, data->enabled)) {
		LOG_ERR("%s: pgood test failed", dev->name);
		data->state = driver_state_failed;
	} else {
		data->state = driver_state_idle;
	}

#if IS_ENABLED(CONFIG_REGULATOR_EXTENDED_MONITOR_PGOODS)
	/* enable pgood callbacks */
	async_pgoods(dev, true);
#endif /* IS_ENABLED(CONFIG_REGULATOR_EXTENDED_MONITOR_PGOODS) */

	/* notify subscribers */
	value_sub_notify(&data->sub, dev, REGEXT_STATE);
}

/*
 * The worker is used for several things:
 *
 * * If a transition occurred in a context where the GPIO state could
 *	 not be changed that's done here.
 * * If a start or stop transition requires a delay that exceeds one
 *	 tick the notification after the delay is performed here.
 */
static void regulator_extended_work(struct k_work *work)
{
	struct k_work_delayable *kwd = k_work_delayable_from_work(work);
	struct driver_data *data = CONTAINER_OF(kwd, struct driver_data, work);
	const struct device *dev = data->dev;
	const struct driver_config *cfg = dev->config;
	uint32_t delay_us = 0;
	int rc = 0;

	__ASSERT_NO_MSG(data->state != driver_state_idle && data->state != driver_state_failed);

	if (data->state == driver_state_defferred) {
		rc = set_enables(data->dev, data->enabled);
		LOG_DBG("%s: work %s: %d", dev->name, data->enabled ? "enable" : "disable", rc);

		if (rc < 0) {
			data->state = driver_state_pending;
		} else {
			delay_us = data->enabled ? cfg->startup_delay_us : cfg->off_on_delay_us;
		}
	} else {
		data->state = driver_state_idle;
		LOG_DBG("%s: work delay complete", dev->name);
	}

	finalize_transition(data, delay_us);
}

static int regulator_extended_get(const struct device *dev, value_id_t id, value_t *pvalue)
{
	struct driver_data *data = dev->data;

	switch (id) {
	case REGEXT_STATE:
		*pvalue = data->enabled;
		if (data->state == driver_state_failed) {
			return -EFAULT;
		} else if (data->state != driver_state_idle) {
			return -EAGAIN;
		}
		break;
	case REGEXT_PGOOD:
		if (!has_pgoods(dev)) {
			*pvalue = -1;
			return -ENOTSUP;
		}
		*pvalue = 0 == test_pgoods(dev, true);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int regulator_extended_set(const struct device *dev, value_id_t id, value_t value)
{
	const struct driver_config *cfg = dev->config;
	struct driver_data *data = dev->data;
	uint32_t delay_us = 0;
	int rc = 0;

	switch (id) {
	case REGEXT_STATE:
		if (cfg->options & OPTION_ALWAYS_ON) {
			return -ENOTSUP;
		}

		/* force change state anyway when failed */
		if (data->state != driver_state_failed) {
			if (data->state != driver_state_idle) {
				/* still in transition state yet */
				LOG_WRN("%s: still in transition state", dev->name);
				return -EAGAIN;
			}

			if (data->enabled == value) {
				/* state already is same as requested, nothing to do */
				return 0;
			}
		}

		data->enabled = value;

#if IS_ENABLED(CONFIG_REGULATOR_EXTENDED_MONITOR_PGOODS)
		/* disable pgood callbacks */
		async_pgoods(dev, false);
#endif /* IS_ENABLED(CONFIG_REGULATOR_EXTENDED_MONITOR_PGOODS) */

		/* turn regulator on/off */
		rc = set_enables(data->dev, data->enabled);
		LOG_DBG("%s: %s: %d", dev->name, data->enabled ? "enabled" : "disabled", rc);

		if (rc == -EWOULDBLOCK) {
			/* Perform the disable and finalization in a work item. */
			LOG_DBG("%s: %s deferred", dev->name, value ? "enable" : "disable");
			data->state = driver_state_defferred;
			k_work_schedule(&data->work, K_NO_WAIT);
			return 0;
		} else if (rc < 0) {
			data->state = driver_state_failed;
		} else {
			delay_us = value ? cfg->startup_delay_us : cfg->off_on_delay_us;
		}

		break;

#if IS_ENABLED(CONFIG_REGULATOR_EXTENDED_SIMULATE_FAULTS)
	case REGEXT_PGOOD:
		data->state = driver_state_failed;

		/* notify subscribers */
		value_sub_notify(&data->sub, dev, REGEXT_STATE);

		break;
#endif /* IS_ENABLED(CONFIG_REGULATOR_EXTENDED_SIMULATE_FAULTS) */

	default:
		return -EINVAL;
	}

	finalize_transition(data, delay_us);
	return 0;
}

static int regulator_extended_sub(const struct device *dev, value_id_t id,
				  struct value_sub_cb *cb, bool sub)
{
	struct driver_data *data = dev->data;

	switch (id) {
	case REGEXT_STATE:
		value_sub_manage(&data->sub, cb, sub);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static const struct value_driver_api regulator_extended_api = {
	.get = regulator_extended_get,
	.set = regulator_extended_set,
	.sub = regulator_extended_sub,
};

static int regulator_extended_init(const struct device *dev)
{
	struct driver_data *data = dev->data;
	const struct driver_config *cfg = dev->config;
	uint32_t delay_us;
	int rc;

	if (cfg->options & (OPTION_ALWAYS_ON | OPTION_BOOT_ON)) {
		delay_us = cfg->startup_delay_us;
		data->enabled = true;
		data->state = driver_state_pending;
	} else {
		delay_us = 0;
		data->state = driver_state_idle;
	}

	rc = init_enables(dev);
	if (rc != 0) {
		goto end;
	}

	rc = init_pgoods(dev);
	if (rc != 0) {
		goto end;
	}

	if (delay_us > 0) {
		/* Turned on and we have to wait until the on
		 * completes.	 Since this is in the driver init we
		 * can't sleep.
		 */
		k_busy_wait(delay_us);
	}

	if (data->state == driver_state_pending) {
		if (0 != test_pgoods(dev, data->enabled)) {
			data->state = driver_state_failed;
			LOG_ERR("%s: Error when initial enable", dev->name);
			rc = -EINVAL;
		} else {
			data->state = driver_state_idle;
			rc = 0;
		}
	}

end:
	LOG_DBG("%s: rc: %i", dev->name, rc);
	return rc;
}

#define REG_GPIO_DT_SPEC_ELEM(node_id, prop, idx) \
	GPIO_DT_SPEC_GET_BY_IDX(node_id, prop, idx),

#define REG_GPIO_CB_INIT(node_id, prop, idx) \
	{},

#if IS_ENABLED(CONFIG_REGULATOR_EXTENDED_MONITOR_PGOODS)
#define MONITOR_PGOODS_EXTRA_DATA(id)					      \
	.marker = {.pin_mask = 0},					      \
	IF_ENABLED(DT_INST_NODE_HAS_PROP(id, pgood_gpios),		      \
		   (.pgood_cb = { DT_INST_FOREACH_PROP_ELEM(id, pgood_gpios,  \
							    REG_GPIO_CB_INIT) \
		    }, ))
#else /* IS_ENABLED(CONFIG_REGULATOR_EXTENDED_MONITOR_PGOODS) */
#define MONITOR_PGOODS_EXTRA_DATA(id)
#endif /* IS_ENABLED(CONFIG_REGULATOR_EXTENDED_MONITOR_PGOODS) */

#define REGULATOR_EXTENDED_DEVICE(id)					       \
									       \
	IF_ENABLED(DT_INST_NODE_HAS_PROP(id, pgood_gpios),		       \
		   (static const struct gpio_dt_spec		       \
		    enable_gpios_##id[] = {				       \
		DT_INST_FOREACH_PROP_ELEM(id, enable_gpios,		       \
					  REG_GPIO_DT_SPEC_ELEM)	       \
	}));								       \
									       \
	IF_ENABLED(DT_INST_NODE_HAS_PROP(id, pgood_gpios),		       \
		   (static const struct pgood_gpio_dt_spec		       \
		    pgood_gpios_##id[] = {				       \
		DT_INST_FOREACH_PROP_ELEM(id, pgood_gpios,		       \
					  REG_GPIO_DT_SPEC_ELEM)	       \
	}));								       \
									       \
	static const struct driver_config regulator_extended_config_##id = {   \
		COND_CODE_1(DT_INST_NODE_HAS_PROP(id, enable_gpios),                \
			    (.enable_gpio = enable_gpios_##id,		       \
			     .num_enables = ARRAY_SIZE(enable_gpios_##id)),      \
			    (.enable_gpio = NULL,			       \
			     .num_enables = 0)),				       \
		COND_CODE_1(DT_INST_NODE_HAS_PROP(id, pgood_gpios),	       \
			    (.pgood_gpio = pgood_gpios_##id,		       \
			     .num_pgoods = ARRAY_SIZE(pgood_gpios_##id)),      \
			    (.pgood_gpio = NULL,			       \
			     .num_pgoods = 0)),				       \
		.startup_delay_us = DT_INST_PROP(id, startup_delay_us),	       \
		.off_on_delay_us = DT_INST_PROP(id, off_on_delay_us),	       \
		.options = 0						       \
			   IF_ENABLED(DT_INST_PROP(id, regulator_boot_on),     \
				      (| OPTION_BOOT_ON))		       \
			   IF_ENABLED(DT_INST_PROP(id, regulator_always_on),   \
				      (| OPTION_ALWAYS_ON)),		       \
	};								       \
									       \
	static struct driver_data regulator_extended_data_##id = {	       \
		.sub = VALUE_SUB_INIT(),				       \
		.dev = DEVICE_DT_GET(DT_DRV_INST(id)),			       \
		.work = Z_WORK_DELAYABLE_INITIALIZER(regulator_extended_work), \
		.state = driver_state_idle,				       \
		.enabled = false,					       \
		MONITOR_PGOODS_EXTRA_DATA(id)				       \
	};								       \
									       \
	DEVICE_DT_INST_DEFINE(id, regulator_extended_init, NULL,	       \
			      &regulator_extended_data_##id,		       \
			      &regulator_extended_config_##id, POST_KERNEL,    \
			      CONFIG_REGULATOR_EXTENDED_INIT_PRIORITY,	       \
			      &regulator_extended_api);

DT_INST_FOREACH_STATUS_OKAY(REGULATOR_EXTENDED_DEVICE)
