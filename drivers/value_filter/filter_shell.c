/*
 * Copyright (c) 2023 MBT
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdlib.h>
#include <zephyr/dt-bindings/value/filter.h>
#include <zephyr/drivers/value.h>
#include <zephyr/shell/shell.h>
#include <zephyr/fixed_point.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(filter_shell, CONFIG_LOG_DEFAULT_LEVEL);

#define DT_DRV_COMPAT FILTER_DT_COMPAT

#define _FILTER_DEVICE(id) DEVICE_DT_GET(DT_DRV_INST(id)),

static const struct device *device_ptr[] = {
	DT_INST_FOREACH_STATUS_OKAY(_FILTER_DEVICE)
};

static const unsigned num_devices = ARRAY_SIZE(device_ptr);

struct io_funcs {
	void (*print_output)(const struct shell *shell,
			     enum shell_vt100_color color,
			     value_t value);
	void (*print_param)(const struct shell *shell,
			    value_t value);
	value_t param_scale;
};

#define _FILTER_IO_FUNC_IMPLS(id)						  \
	static void print_output_##id(const struct shell *shell,		  \
				      enum shell_vt100_color color,		  \
				      value_t value)				  \
	{									  \
		shell_fprintf(shell, color,					  \
			      FIXP_PRI(DT_INST_PROP(id, output_fract_digits))	  \
			      IF_ENABLED(DT_INST_HAS_PROP(id, units),		  \
					 (" " DT_INST_PROP(id, units))),	  \
			      FIXP_R_VAL(value, DT_INST_PROP(id, output_scale),	  \
					 0,					  \
					 DT_INST_PROP(id, output_fract_digits))); \
	}									  \
										  \
	static void print_param_##id(const struct shell *shell,			  \
				     value_t value)				  \
	{									  \
		shell_fprintf(shell, SHELL_NORMAL,				  \
			      FIXP_PRI(DT_INST_PROP(id, param_fract_digits)),	  \
			      FIXP_R_VAL(value, DT_INST_PROP(id, param_scale),	  \
					 0,					  \
					 DT_INST_PROP(id, param_fract_digits)));  \
	}

DT_INST_FOREACH_STATUS_OKAY(_FILTER_IO_FUNC_IMPLS);

#define _FILTER_IO_FUNCS(id)				\
	{ .print_output = print_output_##id,		\
	  .print_param = print_param_##id,		\
	  .param_scale = DT_INST_PROP(id, param_scale),	\
	},

static const struct io_funcs device_io[] = {
	DT_INST_FOREACH_STATUS_OKAY(_FILTER_IO_FUNCS)
};

static const struct {
	value_id_t id;
	const char *name;
} filter_params[] = {
	{ .id = FILTER_ALPHA, .name = "alpha", },
	{ .id = FILTER_SAMPLES, .name = "samples", },
	{ .id = FILTER_WINDOW, .name = "window", },
};

static int cmd_list(const struct shell *shell, size_t argc, char **argv)
{
	const struct device *dev;
	const struct io_funcs *io;
	unsigned i, j, n;
	value_t value;
	int rc;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	shell_print(shell, "Filters:");
	for (i = 0; i < num_devices; i++) {
		dev = device_ptr[i];
		io = &device_io[i];

		value_get(dev, FILTER_STATE, &value);

		shell_fprintf(shell, SHELL_NORMAL, "[%u] %s (%s",
			      i, dev->name, value ? "on" : "off");

		value_get(dev, FILTER_VALUES, &value);

		for (j = 0, n = value; j < n; j++) {
			rc = value_get(dev, FILTER_OUTPUT(j), &value);

			shell_fprintf(shell, SHELL_NORMAL, ", out%u=", j);
			io->print_output(shell, rc == 0 ? SHELL_NORMAL :
					 rc == -EAGAIN ? SHELL_WARNING :
					 SHELL_ERROR, value);
		}

		for (j = 0; j < ARRAY_SIZE(filter_params); j++) {
			value_get(dev, filter_params[j].id, &value);
			shell_fprintf(shell, SHELL_NORMAL, ", %s=", filter_params[j].name);
			io->print_param(shell, value);
		}

		shell_print(shell, ")");
	}
	return 0;
}

enum {
	arg_idx_dev     = 1,
	arg_idx_value   = 2,
};

static int parse_common_args(const struct shell *shell, char **argv)
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

	shell_error(shell, "Filter device %s not found", argv[arg_idx_dev]);
	return -ENODEV;
}

static int cmd_state(const struct shell *shell, size_t argc, char **argv)
{
	const struct device *dev;
	value_t state;
	int rc;

	rc = parse_common_args(shell, argv);
	if (rc < 0) {
		return rc;
	}

	dev = device_ptr[rc];
	state = argv[0][1] == 'n' ? 1 : 0;

	rc = value_set(dev, FILTER_STATE, state);
	if (rc < 0) {
		shell_error(shell, "%s: Error when turning filter %s", dev->name, argv[0]);
	} else {
		shell_print(shell, "%s: Filter turned %s", dev->name, argv[0]);
	}

	return rc;
}

static int cmd_param(const struct shell *shell, size_t argc, char **argv)
{
	const struct device *dev;
	const struct io_funcs *io;
	value_id_t id;
	value_t value;
	int rc;

	rc = parse_common_args(shell, argv);
	if (rc < 0) {
		return rc;
	}
	dev = device_ptr[rc];
	io = &device_io[rc];

	if (argc > 2) {
		rc = fixp_parse(argv[arg_idx_value], io->param_scale, &value);
		if (rc < 0 || argv[arg_idx_value][rc] != '\0') {
			shell_error(shell, "Invalid parameter value");
			return rc;
		}
	}

	switch (argv[0][0]) {
	case 'a':
		id = FILTER_ALPHA;
		break;
	case 's':
		id = FILTER_SAMPLES;
		break;
	case 'w':
		id = FILTER_WINDOW;
		break;
	default:
		return -ENOTSUP;
	}

	if (argc > 2) {
		/* set controller value */
		rc = value_set(dev, id, value);
		if (rc) {
			shell_error(shell, "Error when set parameter: %d", rc);
		}
	} else {
		/* get controller value */
		rc = value_get(dev, id, &value);
		if (rc) {
			shell_error(shell, "Error when get parameter: %d", rc);
		}
		io->print_param(shell, value);
		shell_print(shell, ""); // endline
	}

	return rc;
}

static int cmd_invoke(const struct shell *shell, size_t argc, char **argv)
{
	const struct device *dev;
	value_t value;
	int rc;

	rc = parse_common_args(shell, argv);
	if (rc < 0) {
		return rc;
	}
	dev = device_ptr[rc];

	switch (argv[0][0]) {
#if IS_ENABLED(CONFIG_VALUE_FILTER_SETTINGS)
	case 'l':
		value = FILTER_PARAM_LOAD;
		break;
	case 's':
		value = FILTER_PARAM_SAVE;
		break;
#endif /* IS_ENABLED(CONFIG_VALUE_FILTER_SETTINGS) */
	case 'r':
		value = FILTER_PARAM_RESET;
		break;
	default:
		return -ENOTSUP;
	}

	/* invoke storage action */
	rc = value_set(dev, FILTER_COMMAND, value);
	if (rc) {
		shell_error(shell, "Settings error: %d", rc);
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
	sub_filter,
	SHELL_CMD_ARG(list, NULL, "Show available filters", cmd_list, 1, 0),
	SHELL_CMD_ARG(on, &dev_name, "<device> Enable filter", cmd_state, 2, 0),
	SHELL_CMD_ARG(off, &dev_name, "<device> Disable filter", cmd_state, 2, 0),
	SHELL_CMD_ARG(alpha, &dev_name, "<device> [value] Get/set alpha factor value",
		      cmd_param, 2, 1),
	SHELL_CMD_ARG(samples, &dev_name, "<device> [value] Get/set number of smoothing samples",
		      cmd_param, 2, 1),
	SHELL_CMD_ARG(window, &dev_name, "<device> [value] Get/set smoothing time window",
		      cmd_param, 2, 1),
#if IS_ENABLED(CONFIG_VALUE_FILTER_SETTINGS)
	SHELL_CMD_ARG(load, &dev_name, "<device> Load parameter from settings", cmd_invoke, 2, 0),
	SHELL_CMD_ARG(save, &dev_name, "<device> Save parameter in settings", cmd_invoke, 2, 0),
#endif /* IS_ENABLED(CONFIG_VALUE_FILTER_SETTINGS) */
	SHELL_CMD_ARG(reset, &dev_name, "<device> Reset parameter to default", cmd_invoke, 2, 0),
	SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(filter, &sub_filter, "Filter commands", NULL);
