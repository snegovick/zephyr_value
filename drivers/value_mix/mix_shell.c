/*
 * Copyright (c) 2023 MBT
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdlib.h>
#include <zephyr/dt-bindings/value/mix.h>
#include <zephyr/drivers/value.h>
#include <zephyr/shell/shell.h>
#include <zephyr/fixed_point.h>

#define DT_DRV_COMPAT MIX_DT_COMPAT

#define GET_DEVICE_PTR(id) DEVICE_DT_GET(DT_DRV_INST(id)),

static const struct device *device_ptr[] = {
	DT_INST_FOREACH_STATUS_OKAY(GET_DEVICE_PTR)
};

static const uint8_t num_devices = ARRAY_SIZE(device_ptr);

typedef void (*print_func)(const struct shell *shell,
			   enum shell_vt100_color color,
			   value_t value);

struct io_funcs {
	print_func weight_print;
	value_t weight_scale;
	print_func output_print;
	const char **input_names;
};

#define DEF_IO_FUNCS(id)							 \
	static void print_weight_##id(const struct shell *shell,		 \
				      enum shell_vt100_color color,		 \
				      value_t value)				 \
	{									 \
		shell_fprintf(shell, color,					 \
			      FIXP_PRI(DT_INST_PROP(id, weight_fraction)) " ",	 \
			      FIXP_R_VAL(value, DT_INST_PROP(id, weight_scale),	 \
					 0, DT_INST_PROP(id, weight_fraction))); \
	}									 \
										 \
	static void print_output_##id(const struct shell *shell,		 \
				      enum shell_vt100_color color,		 \
				      value_t value)				 \
	{									 \
		shell_fprintf(shell, color,					 \
			      FIXP_PRI(DT_INST_PROP(id, output_fraction))	 \
			      " " DT_INST_PROP(id, output_units),		 \
			      FIXP_R_VAL(value, DT_INST_PROP(id, output_scale),	 \
					 0, DT_INST_PROP(id, output_fraction))); \
	}									 \
										 \
	static const char *input_names_##id[] = {				 \
		DT_INST_FOREACH_PROP_ELEM(id, value_names, GET_VAL_NAME)	 \
	};

#define GET_VAL_NAME(node_id, prop, idx) \
	DT_PROP_BY_IDX(node_id, prop, idx),

#define GET_IO_FUNCS(id)					\
	{							\
		.weight_print = print_weight_##id,		\
		.weight_scale = DT_INST_PROP(id, weight_scale),	\
		.output_print = print_output_##id,		\
		.input_names = input_names_##id,		\
	},

DT_INST_FOREACH_STATUS_OKAY(DEF_IO_FUNCS);

static const struct io_funcs io_funcs_by_dev[] = {
	DT_INST_FOREACH_STATUS_OKAY(GET_IO_FUNCS)
};

static int cmd_list(const struct shell *shell, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	const struct device *dev;
	unsigned i;
	unsigned wi;
	unsigned wn;
	value_t value;
	int rc;
	const struct io_funcs *io_funcs;

	shell_print(shell, "Mixers:");

	for (i = 0; i < num_devices; i++) {
		dev = device_ptr[i];
		io_funcs = &io_funcs_by_dev[i];

		if (0 != value_get(dev, MIX_STATE, &value)) {
			value = 0;
		}

		shell_fprintf(shell, SHELL_NORMAL, "[%u] %s (%s, out=",
			      i, dev->name, value ? "on" : "off");

		rc = value_get(dev, MIX_OUTPUT, &value);

		io_funcs->output_print(shell, rc == 0 ? SHELL_NORMAL :
				       rc == -EAGAIN ? SHELL_WARNING : SHELL_ERROR,
				       value);

		shell_fprintf(shell, SHELL_NORMAL, ", weights: ");

		value_get(dev, MIX_INPUTS, &value);

		for (wn = value, wi = 0; wi < wn; wi++) {
			shell_fprintf(shell, SHELL_NORMAL, "%s%s=",
				      wi > 0 ? ", " : "", io_funcs->input_names[wi]);

			rc = value_get(dev, MIX_WEIGHT(wi), &value);

			io_funcs->weight_print(shell,
					       rc == 0 ? SHELL_NORMAL :
					       rc == -EAGAIN ? SHELL_WARNING :
					       SHELL_ERROR,
					       value);
		}

		shell_fprintf(shell, SHELL_NORMAL, ")\n");
	}
	return 0;
}

enum {
	arg_idx_dev     = 1,
	arg_idx_wht     = 2,
	arg_idx_val     = 3,
};

static int parse_common_args(const struct shell *shell, char **argv)
{
	unsigned dev_idx;
	char *end_ptr;

	dev_idx = strtoul(argv[arg_idx_dev], &end_ptr, 0);

	if (*end_ptr == '\0') { /* get device by index */
		if (dev_idx < num_devices) {
			goto got_dev;
		}
	} else {        /* get device by name */
		for (dev_idx = 0; dev_idx < num_devices; dev_idx++) {
			if (!strcmp(device_ptr[dev_idx]->name, argv[arg_idx_dev])) {
				goto got_dev;
			}
		}
	}

	shell_error(shell, "Mixer device %s not found", argv[arg_idx_dev]);
	return -ENODEV;

got_dev:
	return dev_idx;
}

static int parse_input_arg(const struct shell *shell,
			   const struct device *dev,
			   const char **names,
			   char **argv)
{
	value_t value;
	unsigned wi;
	unsigned wn;
	char *end_ptr;

	value_get(dev, MIX_INPUTS, &value);
	wn = value;

	wi = strtoul(argv[arg_idx_wht], &end_ptr, 0);

	if (*end_ptr == '\0') { /* get device by index */
		if (wi < wn) {
			goto got_wht;
		}
	} else {        /* get value by name */
		for (wi = 0; wi < wn; wi++) {
			if (!strcmp(names[wi], argv[arg_idx_wht])) {
				goto got_wht;
			}
		}
	}

	shell_error(shell, "Mixer value %s not found", argv[arg_idx_wht]);
	return -EINVAL;
got_wht:
	return wi;
}

static int cmd_onoff(const struct shell *shell, size_t argc, char **argv)
{
	const struct device *dev;
	int rc;

	rc = parse_common_args(shell, argv);
	if (rc < 0) {
		return rc;
	}
	dev = device_ptr[rc];

	rc = value_set(dev, MIX_STATE, argv[0][1] == 'n' ? 1 : 0);

	return rc;
}

static int cmd_weight(const struct shell *shell, size_t argc, char **argv)
{
	const struct device *dev;
	const struct io_funcs *io_funcs;
	value_t value;
	unsigned wi;
	unsigned wn;
	int rc;

	rc = parse_common_args(shell, argv);
	if (rc < 0) {
		return rc;
	}
	dev = device_ptr[rc];
	io_funcs = &io_funcs_by_dev[rc];

	if (argc > 2) { /* get weight name or index */
		wi = parse_input_arg(shell, dev, io_funcs->input_names, argv);
	}

	switch (argc) {
	case 2: // Get weights data
		value_get(dev, MIX_INPUTS, &value);

		for (wn = value, wi = 0; wi < wn; wi++) {
			shell_fprintf(shell, SHELL_NORMAL, "%s%s=",
				      wi > 0 ? ", " : "", io_funcs->input_names[wi]);

			// get weight value
			value_get(dev, MIX_WEIGHT(wi), &value);
			io_funcs->weight_print(shell, SHELL_NORMAL, value);
		}

		shell_print(shell, ""); // line-feed
		break;

	case 3: // Get weight by name or index
		value_get(dev, MIX_WEIGHT(wi), &value);
		io_funcs->weight_print(shell, SHELL_NORMAL, value);
		shell_print(shell, ""); // line-feed
		break;

	case 4: // Set weight by name or index
		rc = fixp_parse(argv[arg_idx_val], io_funcs->weight_scale, &value);
		if (rc < 0 || argv[arg_idx_val][rc] != '\0') {
			shell_error(shell, "Invalid weight value");
		}

		rc = value_set(dev, MIX_WEIGHT(wi), value);
		if (rc != 0) {
			shell_error(shell, "Error when setting weight");
		}

		break;
	}

	return rc;
}

static int cmd_select(const struct shell *shell, size_t argc, char **argv)
{
	const struct device *dev;
	const struct io_funcs *io_funcs;
	value_t value;
	unsigned wi;
	unsigned wn;
	int selected_wi = -1;
	int rc;

	rc = parse_common_args(shell, argv);
	if (rc < 0) {
		return rc;
	}
	dev = device_ptr[rc];
	io_funcs = &io_funcs_by_dev[rc];

	switch (argc) {
	case 2: // Get selected input
		value_get(dev, MIX_INPUTS, &value);

		for (wn = value, wi = 0; wi < wn; wi++) {
			// get weight value
			value_get(dev, MIX_WEIGHT(wi), &value);
			if (value == io_funcs->weight_scale) {
				if (selected_wi != -1) {
					selected_wi = -1;
					break;
				}
				selected_wi = wi;
			}
		}

		if (selected_wi != -1) {
			shell_print(shell, "#%u %s", selected_wi,
				    io_funcs->input_names[selected_wi]);
		} else {
			shell_warn(shell, "Multiple inputs mixed");
			rc = -EINVAL;
		}
		break;

	case 3: // Set selected input
		rc = parse_input_arg(shell, dev, io_funcs->input_names, argv);
		if (rc < 0) {
			break;
		}

		for (wn = value, wi = 0; wi < wn; wi++) {
			// set weight value to 1 only for selected and to 0 for each others
			rc = value_set(dev, MIX_WEIGHT(wi),
				       wi == rc ? io_funcs->weight_scale : 0);
			if (rc != 0) {
				shell_error(shell, "Error when setting weight");
			}
		}
		break;
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
#if IS_ENABLED(CONFIG_DIRECT_CONTROLLER_SETTINGS)
	case 'l':
		value = MIX_WEIGHTS_LOAD;
		break;
	case 's':
		value = MIX_WEIGHTS_SAVE;
		break;
#endif /* IS_ENABLED(CONFIG_DIRECT_CONTROLLER_SETTINGS) */
	case 'r':
		value = MIX_WEIGHTS_RESET;
		break;
	default:
		return -ENOTSUP;
	}

	/* invoke storage action */
	rc = value_set(dev, MIX_COMMAND, value);
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
	sub_vmix,
	SHELL_CMD_ARG(list, NULL, "Show available mixers", cmd_list, 1, 0),
	SHELL_CMD_ARG(on, &dev_name, "<device> Enable mixer", cmd_onoff, 2, 0),
	SHELL_CMD_ARG(off, &dev_name, "<device> Disable mixer", cmd_onoff, 2, 0),
	SHELL_CMD_ARG(weight, &dev_name, "<device> [<input>] [<weight>] Get/Set weights", cmd_weight, 2, 2),
	SHELL_CMD_ARG(select, &dev_name, "<device> <input> Set single input", cmd_select, 3, 0),
#if IS_ENABLED(CONFIG_DIRECT_CONTROLLER_SETTINGS)
	SHELL_CMD_ARG(load, &dev_name, "<device> Load weights from settings", cmd_invoke, 2, 0),
	SHELL_CMD_ARG(save, &dev_name, "<device> Save weights in settings", cmd_invoke, 2, 0),
#endif /* IS_ENABLED(CONFIG_DIRECT_CONTROLLER_SETTINGS) */
	SHELL_CMD_ARG(reset, &dev_name, "<device> Reset weights to default", cmd_invoke, 2, 0),
	SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(vmix, &sub_vmix, "Value mixer commands", NULL);
