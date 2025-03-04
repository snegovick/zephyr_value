#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <stdlib.h>
#include <zephyr/drivers/value.h>
#include <zephyr/dt-bindings/power_graph.h>
#include <zephyr/device.h>

#define DT_DRV_COMPAT PWRGRAPH_DT_COMPAT

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
	int rc;
	value_t state;

	shell_print(shell, "Graphs:");

	for (i = 0; i < num_devices; i++) {
		dev = device_ptr[i];
		rc = value_get(dev, PWRGRAPH_STATE, &state);

		shell_print(shell, "[%i] %s (%sstate: %u)", i, dev->name,
			    rc == -EAGAIN ? "transition to " : rc != 0 ? "failed on " : "", state);
	}

	return 0;
}

enum {
	arg_idx_dev     = 1,
	arg_idx_val     = 2,
};

static int parse_common_args(const struct shell *shell, char **argv, int argc,
			     const struct device **dev, value_t *state)
{
	unsigned num;
	char *end_ptr;

	num = strtoul(argv[arg_idx_dev], &end_ptr, 0);

	if (*end_ptr == '\0') { /* get device by index */
		if (num < num_devices) {
			*dev = device_ptr[num];
			goto got_dev;
		}
	} else {        /* get device by name */
		for (num = 0; num < num_devices; num++) {
			*dev = device_ptr[num];
			if (!strcmp((*dev)->name, argv[arg_idx_dev])) {
				goto got_dev;
			}
		}
	}

	*dev = NULL;
got_dev:

	if (!*dev) {
		shell_error(shell, "Power graph %s not found", argv[arg_idx_dev]);
		return -ENODEV;
	}

	if (argc > 2) {
		num = strtoul(argv[arg_idx_val], &end_ptr, 0);

		if (*end_ptr == '\0') { /*  */
			*state = num;
			goto got_val;
		}

		return -EINVAL;
	}

got_val:
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

static int cmd_state(const struct shell *shell, size_t argc, char **argv)
{
	ARG_UNUSED(argc);

	const struct device *dev;
	value_t new_state = -1;
	value_t state;
	int rc;

	rc = parse_common_args(shell, argv, argc, &dev, &new_state);
	if (rc < 0) {
		if (argc > 2) {
			shell_print(shell, "Invalid state value");
		}
		return rc;
	}

	if (argc > 2) {
		/* set new state */
		rc = value_set(dev, PWRGRAPH_STATE, new_state);
		if (rc) {
			shell_print(shell, "Error when setting graph state (rc: %i)", rc);
			return rc;
		}

		rc = value_get(dev, PWRGRAPH_STATE, &state);
		if (rc == -EAGAIN) {
			struct onoff_lock lock;

			value_sub_cb_init(&lock.cb, onoff_cb);
			k_sem_init(&lock.sem, 0, 1);

			value_sub(dev, PWRGRAPH_STATE, &lock.cb, true);
			rc = k_sem_take(&lock.sem, K_MSEC(5000));
			value_sub(dev, PWRGRAPH_STATE, &lock.cb, false);

			if (rc) {
				shell_print(shell, "Timeout reached when changing graph state (rc: %i)", rc);
			} else {
				value_get(dev, PWRGRAPH_STATE, &state);
			}
		} else if (rc < 0) {
			shell_print(shell, "Error when changing graph state");
		} else {
			shell_print(shell, "New state: %u", state);
		}
	} else {
		rc = value_get(dev, PWRGRAPH_STATE, &state);
		if (rc == -EAGAIN) {
			shell_print(shell, "In transition to state: %u", state);
		} else if (rc < 0) {
			shell_print(shell, "Unable to get current state");
		} else {
			shell_print(shell, "State: %u", state);
		}
	}

	return rc;
}

/* Creating subcommands (level 1 command) array for command "ps". */
SHELL_STATIC_SUBCMD_SET_CREATE(
	sub_power_graph,
	SHELL_CMD_ARG(list, NULL, "List available graphs", cmd_list, 1, 0),
	SHELL_CMD_ARG(state, NULL, "<device> [<state>] Get/set state", cmd_state, 2, 1),
	SHELL_SUBCMD_SET_END);
/* Creating root (level 0) command "ps" */
SHELL_CMD_REGISTER(pwrgraph, &sub_power_graph, "Power graph controls", NULL);
