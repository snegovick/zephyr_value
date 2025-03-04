#include <zephyr/shell/shell.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/value.h>
#include <stdlib.h>
#include <zephyr/dt-bindings/regulator_extended.h>

LOG_MODULE_REGISTER(shell_reg, CONFIG_LOG_DEFAULT_LEVEL);

#define DT_DRV_COMPAT REGEXT_DT_COMPAT

#define GET_DEVICE_PTR(id) DEVICE_DT_GET(DT_DRV_INST(id)),

static const struct device *device_ptr[] = {
	DT_INST_FOREACH_STATUS_OKAY(GET_DEVICE_PTR)
};

static const size_t num_devices = ARRAY_SIZE(device_ptr);

static int cmd_list(const struct shell *shell, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	const struct device *dev;
	size_t i;
	value_t state;
	value_t pgood;
	int rc;

	shell_print(shell, "Regulators:");

	for (i = 0; i < num_devices; i++) {
		dev = device_ptr[i];

		rc = value_get(dev, REGEXT_STATE, &state);
		value_get(dev, REGEXT_PGOOD, &pgood);

		shell_print(shell, "[%i] %s (state: %s%s%s, pgood: %s)", i, dev->name,
			    (rc == -EAGAIN) ? "pending " : "",
			    (rc == -EFAULT) ? "failed " : "",
			    state ? "on" : "off",
			    pgood < 0 ? "-" : pgood ? "yes" : "no");
	}

	return 0;
}

enum {
	arg_idx_dev = 1,
};

static int parse_common_args(const struct shell *shell,
			     char **argv,
			     const struct device **dev)
{
	size_t dev_idx;
	char *end_ptr;

	dev_idx = strtoul(argv[arg_idx_dev], &end_ptr, 0);

	if (*end_ptr == '\0') { /* get device by index */
		if (dev_idx < num_devices) {
			*dev = device_ptr[dev_idx];
			goto got_dev;
		}
	} else {        /* get device by name */
		for (dev_idx = 0; dev_idx < num_devices; dev_idx++) {
			*dev = device_ptr[dev_idx];
			if (!strcmp((*dev)->name, argv[arg_idx_dev])) {
				goto got_dev;
			}
		}
	}

	*dev = NULL;
got_dev:

	if (!*dev) {
		shell_error(shell, "Regulator device %s not found", argv[arg_idx_dev]);
		return -ENODEV;
	}

	return 0;
}

struct onoff_lock {
	struct value_sub_cb cb;
	struct k_sem sem;
};

static void onoff_cb(struct value_sub_cb *cb,
		     const struct device *dev,
		     value_id_t id)
{
	struct onoff_lock *lock = CONTAINER_OF(cb, struct onoff_lock, cb);

	k_sem_give(&lock->sem);
}

static int cmd_onoff(const struct shell *shell, size_t argc, char **argv)
{
	ARG_UNUSED(argc);

	const struct device *dev;
	value_t new_state;
	value_t state;
	int rc;

	rc = parse_common_args(shell, argv, &dev);
	if (rc < 0) {
		return rc;
	}

	new_state = argv[0][1] == 'n' /* on */;

	rc = value_set(dev, REGEXT_STATE, new_state);
	if (rc) {
		shell_print(shell, "Error when setting regulator state (rc: %i)", rc);
		return rc;
	}

	rc = value_get(dev, REGEXT_STATE, &state);
	if (rc == -EAGAIN) {
		struct onoff_lock lock;

		value_sub_cb_init(&lock.cb, onoff_cb);
		k_sem_init(&lock.sem, 0, 1);

		value_sub(dev, REGEXT_STATE, &lock.cb, true);
		rc = k_sem_take(&lock.sem, K_MSEC(5000));
		value_sub(dev, REGEXT_STATE, &lock.cb, false);

		if (rc) {
			shell_print(shell, "Timeout reached when changing regulator state (rc: %i)", rc);
			return rc;
		} else {
			rc = value_get(dev, REGEXT_STATE, &state);
		}
	}

	if (rc == -ECANCELED) {
		shell_print(shell, "Error when changing regulator state");
	} else {
		shell_print(shell, "Regulator is %s", state ? "on" : "off");
	}

	return 0;
}

#if IS_ENABLED(CONFIG_REGULATOR_EXTENDED_SIMULATE_FAULTS)
static int cmd_fail(const struct shell *shell, size_t argc, char **argv)
{
	ARG_UNUSED(argc);

	const struct device *dev;
	int rc;

	rc = parse_common_args(shell, argv, &dev);
	if (rc < 0) {
		return rc;
	}

	rc = value_set(dev, REGEXT_PGOOD, 0);
	if (rc) {
		shell_print(shell, "Error when simulating fault (rc: %i)", rc);
		return rc;
	}

	return 0;
}
#endif /* IS_ENABLED(CONFIG_REGULATOR_EXTENDED_SIMULATE_FAULTS) */

/* Creating subcommands (level 1 command) array for command "reg". */
SHELL_STATIC_SUBCMD_SET_CREATE(
	sub_reg,
	SHELL_CMD_ARG(list, NULL, "List available regulators", cmd_list, 1, 0),
	SHELL_CMD_ARG(on, NULL, "<device> Enable regulator", cmd_onoff, 2, 0),
	SHELL_CMD_ARG(off, NULL, "<device> Disable regulator", cmd_onoff, 2, 0),
#if IS_ENABLED(CONFIG_REGULATOR_EXTENDED_SIMULATE_FAULTS)
	SHELL_CMD_ARG(fail, NULL, "<device> Simulate regulator fault", cmd_fail, 2, 0),
#endif /* IS_ENABLED(CONFIG_REGULATOR_EXTENDED_SIMULATE_FAULTS) */
	SHELL_SUBCMD_SET_END);
/* Creating root (level 0) command "reg" */
SHELL_CMD_REGISTER(reg, &sub_reg, "Regulator controls", NULL);
