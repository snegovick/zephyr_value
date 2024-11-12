/*
 * Copyright (c) 2023 MBT
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/dt-bindings/value/sync.h>
#include <zephyr/drivers/value.h>
#include <zephyr/shell/shell.h>
#include <stdlib.h>

#if IS_ENABLED(CONFIG_VALUE_SYNC_TIMING)
#include <timing/timing.h>
#endif /* IS_ENABLED(CONFIG_VALUE_SYNC_TIMING) */

#define DT_DRV_COMPAT SYNC_DT_COMPAT

#define SYNC_DEVICE(id) DEVICE_DT_GET(DT_DRV_INST(id)),

static const struct device *device_ptr[] = {
	DT_INST_FOREACH_STATUS_OKAY(SYNC_DEVICE)
};

static const size_t num_devices = ARRAY_SIZE(device_ptr);

static int cmd_list(const struct shell *shell, size_t argc, char **argv)
{
	const struct device *dev;
	size_t i;
	value_t state;

#if IS_ENABLED(CONFIG_VALUE_SYNC_TIMING)
	value_t min_cycles;
	value_t max_cycles;
#endif /* IS_ENABLED(CONFIG_VALUE_SYNC_TIMING) */

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	shell_print(shell, "Sync devices:");
	for (i = 0; i < num_devices; i++) {
		dev = device_ptr[i];

		value_get(dev, SYNC_STATE, &state);
#if IS_ENABLED(CONFIG_VALUE_SYNC_TIMING)
		value_get(dev, SYNC_MIN_CYCLES, &min_cycles);
		value_get(dev, SYNC_MAX_CYCLES, &max_cycles);
#endif /* IS_ENABLED(CONFIG_VALUE_SYNC_TIMING) */

#if IS_ENABLED(CONFIG_VALUE_SYNC_TIMING)
		shell_print(shell,
			    "[%i] %s: %s (timing [cycles]: min=%d (%d nS), max=%d (%d nS))",
			    i, dev->name, state ? "on" : "off",
			    min_cycles, (uint32_t)timing_cycles_to_ns(min_cycles),
			    max_cycles, (uint32_t)timing_cycles_to_ns(max_cycles));
#else /* IS_ENABLED(CONFIG_VALUE_SYNC_TIMING) */
		shell_print(shell,
			    "[%i] %s: %s",
			    i, dev->name, state ? "on" : "off");
#endif /* IS_ENABLED(CONFIG_VALUE_SYNC_TIMING) */
	}
	return 0;
}

enum {
	arg_idx_dev     = 1,
	arg_idx_value   = 2,
};

static int parse_common_args(const struct shell *shell,
			     char **argv)
{
	unsigned dev_idx;
	char *end_ptr;

	dev_idx = strtoul(argv[arg_idx_dev], &end_ptr, 0);

	if (*end_ptr == '\0') { /* get device by index */
		if (dev_idx < num_devices) {
			return dev_idx;
		}
	} else {        /* get device by name */
		for (dev_idx = 0; dev_idx < num_devices; dev_idx++) {
			if (!strcmp(device_ptr[dev_idx]->name, argv[arg_idx_dev])) {
				return dev_idx;
			}
		}
	}

	shell_error(shell, "sync device %s not found", argv[arg_idx_dev]);
	return -ENODEV;
}

static int cmd_state(const struct shell *shell, size_t argc, char **argv)
{
	const struct device *dev;
	int rc;
	value_t state;

	rc = parse_common_args(shell, argv);
	if (rc < 0) {
		return rc;
	}
	dev = device_ptr[rc];
	state = argv[0][1] == 'n' ? 1 : 0;

	rc = value_set(dev, SYNC_STATE, state);
	if (rc < 0) {
		shell_print(shell, "%s: Error when turning %s", dev->name, argv[0]);
	} else {
		shell_print(shell, "%s: Sync turned %s", dev->name, argv[0]);
	}

	return rc;
}

static void dev_name_get(size_t idx, struct shell_static_entry *entry)
{
	entry->syntax = idx < num_devices ? device_ptr[idx]->name : NULL;
	entry->handler = NULL;
	entry->help = NULL;
	entry->subcmd = NULL;
}

SHELL_DYNAMIC_CMD_CREATE(dev_name, dev_name_get);

SHELL_STATIC_SUBCMD_SET_CREATE(
	sub_valsync,
	SHELL_CMD_ARG(list, NULL, "Show available sync devices", cmd_list, 1, 0),
	SHELL_CMD_ARG(on, &dev_name, "<device> Enable sync", cmd_state, 2, 0),
	SHELL_CMD_ARG(off, &dev_name, "<device> Disable sync", cmd_state, 2, 0),
	SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(valsync, &sub_valsync, "Value sync commands", NULL);
