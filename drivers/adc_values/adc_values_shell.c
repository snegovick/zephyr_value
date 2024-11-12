/*
 * Copyright (c) 2023 MBT
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdlib.h>
#include <zephyr/dt-bindings/value/adc.h>
#include <zephyr/drivers/value.h>
#include <zephyr/shell/shell.h>

#define DT_DRV_COMPAT ADC_VALUES_DT_COMPAT

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
	value_t num_channels;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	shell_print(shell, "ADC polling devices:");
	for (i = 0; i < num_devices; i++) {
		dev = device_ptr[i];

		value_get(dev, ADC_VALUES_STATE, &state);
		value_get(dev, ADC_VALUES_NUM_CHANNELS, &num_channels);

		shell_print(shell, "[%i] %s (chs: %d): %s",
			    i, dev->name, num_channels,
			    state ? "on" : "off");
	}
	return 0;
}

enum {
	arg_idx_dev     = 1,
	arg_idx_chn     = 2,
};

static int parse_common_args(const struct shell *shell,
			     char **argv,
			     const struct device **dev,
			     int *pchn)
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
		shell_error(shell, "ADC poller device %s not found", argv[arg_idx_dev]);
		return -ENODEV;
	}

	if (pchn) {
		*pchn = strtol(argv[arg_idx_chn], &end_ptr, 0);

		if (*end_ptr != '\0') {
			shell_error(shell, "Invalid channel %s", argv[arg_idx_chn]);
			return -EINVAL;
		}
	}

	return 0;
}

static int cmd_state(const struct shell *shell, size_t argc, char **argv)
{
	const struct device *dev;
	int rc;
	value_t state;

	rc = parse_common_args(shell, argv, &dev, NULL);
	if (rc < 0) {
		return rc;
	}

	state = argv[0][1] == 'n' ? 1 : 0;

	rc = value_set(dev, ADC_VALUES_STATE, state);
	if (rc < 0) {
		shell_print(shell, "%s: Error when turning %s", dev->name, argv[0]);
	} else {
		shell_print(shell, "%s: Sync turned %s", dev->name, argv[0]);
	}

	return rc;
}

static int cmd_read(const struct shell *shell, size_t argc, char **argv)
{
	const struct device *dev;
	int chn;
	value_t value;
	int rc;

	rc = parse_common_args(shell, argv, &dev, &chn);
	if (rc < 0) {
		return rc;
	}

	rc = value_get(dev, ADC_VALUES_CHANNEL(chn), &value);
	if (rc < 0) {
		shell_print(shell, "%s: Error when reading channel #%d", dev->name, chn);
	} else {
		shell_print(shell, "%d", value);
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
	sub_adcvals,
	SHELL_CMD_ARG(list, NULL, "Show available devices", cmd_list, 1, 0),
	SHELL_CMD_ARG(on, &dev_name, "<device> Enable polling", cmd_state, 2, 0),
	SHELL_CMD_ARG(off, &dev_name, "<device> Disable polling", cmd_state, 2, 0),
	SHELL_CMD_ARG(get, &dev_name, "<device> <channel> Get channel value", cmd_read, 3, 0),
	SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(adcvals, &sub_adcvals, "Value sync commands", NULL);
