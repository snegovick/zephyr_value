#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/value.h>
#include <zephyr/dt-bindings/regulator_extended.h>
#include <zephyr/dt-bindings/power_graph.h>

#include <zephyr/logging/log_ctrl.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(power_graph, CONFIG_POWER_GRAPH_LOG_LEVEL);

#define DT_DRV_COMPAT PWRGRAPH_DT_COMPAT

/** State numeric identifier type */
typedef uint8_t state_id_t;
#define NO_STATE UINT8_MAX

#define POWER_GRAPH_PACKED_STAGE_NUM	  \
	(CONFIG_POWER_GRAPH_NUM_DEVICES * \
	 CONFIG_POWER_GRAPH_NUM_STATES)

#if POWER_GRAPH_PACKED_STAGE_NUM <= 256
/** Packed stage type */
typedef uint8_t packed_stage_t;
#else
/** Packed stage type */
typedef uint16_t packed_stage_t;
#endif

#if CONFIG_POWER_GRAPH_NUM_STAGES <= 128
/** Stage number type */
typedef uint8_t stage_idx_t;
/** Invalid stage */
#define NO_STAGE UINT8_MAX
#else
/** Stage number type */
typedef uint16_t stage_idx_t;
/** Invalid stage */
#define NO_STAGE UINT16_MAX
#endif

#if CONFIG_POWER_GRAPH_NUM_DEVICES <= 128
/** Device number type */
typedef uint8_t dev_idx_t;
#define NO_DEV UINT8_MAX
#else
/** Device number type */
typedef uint16_t dev_idx_t;
#define NO_DEV UINT16_MAX
#endif

#define _LOG2_V(exp, val) (val) <= (1 << (exp)) ? (exp)
#define _LOG2(val) (FOR_EACH_FIXED_ARG(_LOG2_V, (:), val,	    \
				       0, 1, 2, 3, 4, 5, 6, 7, 8,   \
				       9, 10, 11, 12, 13, 14, 15) : \
			    (1 << 16))

BUILD_ASSERT(_LOG2(2) == 1, "_LOG2(2)");
BUILD_ASSERT(_LOG2(3) == 2, "_LOG2(3)");
BUILD_ASSERT(_LOG2(4) == 2, "_LOG2(4)");
BUILD_ASSERT(_LOG2(5) == 3, "_LOG2(5)");
BUILD_ASSERT(_LOG2(6) == 3, "_LOG2(6)");
BUILD_ASSERT(_LOG2(7) == 3, "_LOG2(7)");
BUILD_ASSERT(_LOG2(8) == 3, "_LOG2(8)");
BUILD_ASSERT(_LOG2(9) == 4, "_LOG2(9)");

#define _BITS_MASK(bits) (~(~0ull << (bits)))

BUILD_ASSERT(_BITS_MASK(0) == 0, "_BITS_MASK(0)");
BUILD_ASSERT(_BITS_MASK(1) == 1, "_BITS_MASK(1)");
BUILD_ASSERT(_BITS_MASK(2) == 3, "_BITS_MASK(2)");
BUILD_ASSERT(_BITS_MASK(3) == 7, "_BITS_MASK(3)");
BUILD_ASSERT(_BITS_MASK(4) == 15, "_BITS_MASK(4)");
BUILD_ASSERT(_BITS_MASK(5) == 31, "_BITS_MASK(5)");
BUILD_ASSERT(_BITS_MASK(6) == 63, "_BITS_MASK(6)");
BUILD_ASSERT(_BITS_MASK(7) == 127, "_BITS_MASK(7)");
BUILD_ASSERT(_BITS_MASK(8) == 255, "_BITS_MASK(8)");

#define PACKED_SPAGE_BITS (sizeof(packed_stage_t) * 8)
#define DEV_INDEX_BITS _LOG2(CONFIG_POWER_GRAPH_NUM_DEVICES)
#define DEV_INDEX_MASK _BITS_MASK(DEV_INDEX_BITS)
#define DEV_STATE_BITS _LOG2(CONFIG_POWER_GRAPH_NUM_STATES)
#define DEV_STATE_MASK _BITS_MASK(DEV_STATE_BITS)

/* [stage_state_bits|stage_index_bits]  */
#define STAGE_PACK(dev_index, dev_state) \
	((dev_index) | ((dev_state) << DEV_INDEX_BITS))
#define DEV_INDEX(packed_stage)	\
	((packed_stage) & DEV_INDEX_MASK)
#define DEV_STATE(packed_stage)	\
	((packed_stage) >> DEV_INDEX_BITS)

BUILD_ASSERT(DEV_INDEX(STAGE_PACK(0, DEV_STATE_MASK)) == 0, "Dev. index: 0");
BUILD_ASSERT(DEV_INDEX(STAGE_PACK(1, DEV_STATE_MASK)) == 1, "Dev. index: 1");
BUILD_ASSERT(DEV_INDEX(STAGE_PACK(DEV_INDEX_MASK, DEV_STATE_MASK))
	     == DEV_INDEX_MASK, "Dev. index: MAX");
BUILD_ASSERT(DEV_STATE(STAGE_PACK(DEV_INDEX_MASK, 0)) == 0, "Dev. state: 0");
BUILD_ASSERT(DEV_STATE(STAGE_PACK(DEV_INDEX_MASK, 1)) == 1, "Dev. state: 1");
BUILD_ASSERT(DEV_STATE(STAGE_PACK(DEV_INDEX_MASK, DEV_STATE_MASK))
	     == DEV_STATE_MASK, "Dev. state: MAX");

struct power_state {
	state_id_t id;
};

enum power_transition_flags {
	power_transition_ignore_faults = 1 << 0,
};

struct power_transition {
	const packed_stage_t *stages;
	stage_idx_t num_stages;
	state_id_t initial;
	state_id_t target;
	uint8_t flags;
};

struct driver_config {
	const struct value_dt_spec *specs;
	const struct power_state *states;
	const struct power_transition *transitions;
	dev_idx_t num_specs;
	state_id_t num_states;
	state_id_t num_transitions;
	state_id_t safe_state;
};

#if CONFIG_POWER_GRAPH_FAULT_LOG > 0
struct power_fault {
	state_id_t transition;
	stage_idx_t stage;
	dev_idx_t spec;
};
#endif

struct driver_data {
	struct value_sub sub;   /* list of value subscriptions */
	struct k_work work;     /* work for async tasks */
	state_id_t state;       /* current state */
	state_id_t new_state;   /* new state */
	state_id_t transition;  /* current transition or NO_STATE */
	stage_idx_t stage;      /* current stage or NO_STAGE */
#if CONFIG_POWER_GRAPH_FAULT_LOG > 0
	struct power_fault faults[CONFIG_POWER_GRAPH_FAULT_LOG];
	uint8_t num_faults;             /* number of logged faults */
	uint8_t last_fault;             /* latest logged fault */
#endif
	const struct device *dev;       /* the pointer to device for callbacks */
	struct value_sub_cb marker;            /* always zero to mark the head of callbacks */
	struct value_sub_cb cbs[];      /* num of subscriptions is same as num of devices */
};

/* check marker field alignment (for case when structure layout has changed) */
BUILD_ASSERT(offsetof(struct driver_data, marker) ==
	     offsetof(struct driver_data, cbs[-1]),
	     "Invalid marker field alignment");

#if CONFIG_POWER_GRAPH_FAULT_LOG > 0
static void put_fault(const struct device *dev, dev_idx_t spec)
{
	struct driver_data *data = dev->data;

	if ((data->transition == NO_STATE ||
	     data->stage == NO_STAGE) && spec == NO_DEV) {
		return;
	}

	if (data->num_faults > 0) {
		// increment last fault index
		data->last_fault = (data->last_fault + 1) %
				   CONFIG_POWER_GRAPH_FAULT_LOG;
	}

	data->faults[data->last_fault] = (struct power_fault){
		.transition = data->transition,
		.stage = data->stage,
		.spec = spec,
	};

	if (data->num_faults < CONFIG_POWER_GRAPH_FAULT_LOG) {
		data->num_faults++;
	}
}

static uint8_t num_faults(const struct device *dev)
{
	struct driver_data *data = dev->data;

	return data->num_faults;
}

static struct power_fault *get_fault(const struct device *dev, uint8_t depth)
{
	struct driver_data *data = dev->data;

	if (depth > data->num_faults) {
		return NULL;
	}

	uint8_t index = (depth > data->last_fault ? data->num_faults : 0) +
			data->last_fault - depth;

	return &data->faults[index];
}
#else
#define put_fault(dev, spec)
#define num_faults(dev) 0
#define get_fault(dev, idx) NULL
#endif

static int find_state_by_id(const struct device *dev, state_id_t id)
{
	const struct driver_config *cfg = dev->config;
	unsigned i;

	for (i = 0; i < cfg->num_states; i++) {
		if (cfg->states[i].id == id) {
			return i;
		}
	}

	return -ENOENT;
}

static int find_first_transition_to_state(const struct device *dev, state_id_t state, unsigned *hops)
{
	const struct driver_config *cfg = dev->config;
	struct driver_data *data = dev->data;
	const struct power_transition *transition = cfg->transitions;
	int best_transition_id = -ENOENT;
	unsigned best_hops = UINT_MAX;  // set a known bad value
	int last_transition_id;
	unsigned last_hops;
	unsigned i;

	(*hops)++;

	for (i = 0; i < cfg->num_transitions; i++) {
		transition = &cfg->transitions[i];

		if (transition->target != state) {
			continue;
		}

		if (transition->initial == data->state) {
			// first transition found
			best_transition_id = i;
			break;
		}

		if (*hops >= cfg->num_transitions) {
			continue;
		}

		last_hops = *hops;
		// try find previous transition (to initial state of current)
		last_transition_id = find_first_transition_to_state(dev, transition->initial, &last_hops);

		if (last_transition_id == -ENOENT) {
			// no transition found
			continue;
		}

		if (last_hops < best_hops) {
			// better transition found
			best_hops = last_hops;
			best_transition_id = last_transition_id;
		}
	}

	return best_transition_id;
}

static int find_first_transition(const struct device *dev)
{
	struct driver_data *data = dev->data;
	int transition_id;
	unsigned hops = 0;

	transition_id = find_first_transition_to_state(dev, data->new_state, &hops);

	if (hops > 2) {
		LOG_WRN("%s: Too many chained transitions (%u)", dev->name, hops);
	}

	return transition_id;
}

static void power_graph_task(const struct device *dev)
{
	const struct driver_config *cfg = dev->config;
	struct driver_data *data = dev->data;

	const struct power_transition *transition;
	stage_idx_t stage;
	const struct value_dt_spec *spec;
	value_t desired_state;
	value_t val;
	int rc;

	for (; ;) {
		if (data->new_state == data->state) {
			LOG_DBG("%s: Changed graph state (%u)", dev->name, data->state);
			/* notify subscribers */
			value_sub_notify(&data->sub, dev, PWRGRAPH_STATE);
			return;
		}

		for (; ;) {
			if (data->transition == NO_STATE) {
				rc = find_first_transition(dev);
				if (rc < 0) {
					LOG_ERR("%s: Unable to find transition: %u => %u", dev->name,
						data->state, data->new_state);
					/* try switch to the safe state */
					data->new_state = cfg->safe_state;
					break;
				}
				data->transition = rc;
				data->stage = 0;
				LOG_DBG("%s: Started transition: #%d %u => %u", dev->name,
					data->transition,
					cfg->transitions[data->transition].initial,
					cfg->transitions[data->transition].target);
			}

			/* get current stage */
			transition = &cfg->transitions[data->transition];
			stage = transition->stages[data->stage];
			spec = &cfg->specs[DEV_INDEX(stage)];
			desired_state = DEV_STATE(stage);

			rc = value_get_dt(spec, &val);
			if (rc == -EAGAIN) {
				/* still in transition */
				return;
			}

			if (rc == -EFAULT || rc == -ECANCELED) {
				/* in failed state */
				LOG_ERR("%s: Failed to change state (stage: %i, device: %s)", dev->name,
					data->stage, spec->dev->name);
				if (!(transition->flags & power_transition_ignore_faults)) {
					goto fault;
				}
			} else if (rc != 0) {
				LOG_ERR("%s: Failed to get current state (rc: %i, stage: %i, device: %s)",
					dev->name, rc, data->stage, spec->dev->name);
				goto fault;
			}

			if (val == desired_state) {
				/* already in desired state
				   go to the next stage immediately */
				data->stage++;

				if (data->stage < transition->num_stages) {
					LOG_DBG("%s: Continued transition: #%d %u => %u (stage: %u/%u)",
						dev->name, data->transition, transition->initial,
						transition->target, data->stage, transition->num_stages);
					continue;
				}

				LOG_DBG("%s: Finished transition: #%d %u => %u", dev->name,
					data->transition, transition->initial, transition->target);
				/* already had in final stage
				   go to the next transition */
				data->state = transition->target;
				data->transition = NO_STATE;
				data->stage = NO_STAGE;
				break;
			}

			/* set the desired state */
			rc = value_set_dt(spec, desired_state);
			if (rc != 0) {
				LOG_ERR("%s: Failed to set new state (rc: %i, stage: %i, device: %s)",
					dev->name, rc, data->stage, spec->dev->name);
				goto fault;
			} else {
				/*LOG_DBG("%s: Changed state (stage: %i)", dev->name,
				   data->stage);*/
				continue;
			}

fault:
			put_fault(dev, DEV_INDEX(stage));
			/* any failure happenned
			   force finalize transition */
			data->state = transition->target;
			data->transition = NO_STATE;
			data->stage = NO_STAGE;
			/* and switch to the safe state */
			data->new_state = cfg->safe_state;
			break;
		}
	}
}

static void power_graph_worker(struct k_work *work)
{
	struct driver_data *data = CONTAINER_OF(work, struct driver_data, work);

	power_graph_task(data->dev);
}

static int power_graph_init(const struct device *dev)
{
	const struct driver_config *cfg = dev->config;
	struct driver_data *data = dev->data;
	const struct value_dt_spec *spec;
	unsigned i;
	int rc;

	k_work_init(&data->work, power_graph_worker);

	/* subscribe to state changes */
	for (i = 0; i < cfg->num_specs; i++) {
		spec = &cfg->specs[i];
		/* try subscribe to value changes */
		rc = value_sub_dt(spec, &data->cbs[i], true);
		if (rc) {
			LOG_DBG("%s: Unable to subscribe (spec: %u, rc: %i)",
				dev->name, i, rc);
		} else {
			LOG_DBG("%s: Subscribed to state changes (spec: %u)",
				dev->name, i);
		}
	}

	return 0;
}

static const struct device *device_from_cb(struct value_sub_cb *cb)
{
	/* go up to zero field */
	for (; cb->func != NULL; cb--);

	return CONTAINER_OF(cb, struct driver_data, marker)->dev;
}

static dev_idx_t find_spec(const struct device *dev,
			   const struct device *pwr_dev,
			   value_id_t val_id)
{
	const struct driver_config *cfg = dev->config;
	dev_idx_t i;

	for (i = 0; i < cfg->num_specs; i++) {
		if (cfg->specs[i].dev == pwr_dev &&
		    cfg->specs[i].id == val_id) {
			return i;
		}
	}

	return NO_DEV;
}

static void power_graph_cb(struct value_sub_cb *cb,
			   const struct device *pwr_dev,
			   value_id_t val_id)
{
	const struct device *dev = device_from_cb(cb);
	const struct driver_config *cfg = dev->config;
	struct driver_data *data = dev->data;

	if (data->new_state == data->state) {
		/* not in transition */
		/* any state changes shall be treated as power fails */

		put_fault(dev, find_spec(dev, pwr_dev, val_id));

		data->new_state = cfg->safe_state;
	}

	k_work_submit(&data->work);
}

static int power_graph_get(const struct device *dev,
			   value_id_t id,
			   value_t *pvalue)
{
	const struct driver_config *cfg = dev->config;
	struct driver_data *data = dev->data;

	switch (id) {
	case PWRGRAPH_STATE:
		if (data->transition == NO_STATE) {
			*pvalue = data->state;
		} else {
			*pvalue = cfg->transitions[data->transition].target;
			return -EAGAIN;
		}
		break;
	case PWRGRAPH_NUM_FAULTS:
		*pvalue = num_faults(dev);
		break;
	default:
		if (id & PWRGRAPH_FAULT_DATA_MASK) {
			struct power_fault *fault =
				get_fault(dev, PWRGRAPH_FAULT_DEPTH(id));

			if (fault == NULL) {
				return -ERANGE;
			}

			switch (PWRGRAPH_FAULT_DATA(id)) {
			case PWRGRAPH_FAULT_DATA_TRANSITION:
				if (fault->transition == NO_STATE) {
					*pvalue = -1;
					return -ENODATA;
				} else {
					*pvalue = fault->transition;
				}
				break;
			case PWRGRAPH_FAULT_DATA_STAGE:
				if (fault->stage == NO_STAGE) {
					*pvalue = -1;
					return -ENODATA;
				} else {
					*pvalue = fault->stage;
				}
				break;
			case PWRGRAPH_FAULT_DATA_SPEC:
				if (fault->spec == NO_DEV) {
					*pvalue = -1;
					return -ENODATA;
				} else {
					*pvalue = fault->spec;
				}
				break;
			default:
				return -EINVAL;
			}
		} else {
			return -EINVAL;
		}
	}

	return 0;
}

static int power_graph_set(const struct device *dev,
			   value_id_t id,
			   value_t value)
{
	struct driver_data *data = dev->data;

	switch (id) {
	case PWRGRAPH_STATE:
		if (data->state == value) {
			/* state already is same as requested, nothing to do */
			break;
		}

		if (data->transition != NO_STATE) {
			/* still in transition yet */
			LOG_WRN("%s: still in transition", dev->name);
			return -EBUSY;
		}

		if (find_state_by_id(dev, value) < 0) {
			/* unknown state id */
			return -EINVAL;
		}

		data->new_state = value;

		k_work_submit(&data->work);

		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int power_graph_sub(const struct device *dev,
			   value_id_t id,
			   struct value_sub_cb *cb,
			   bool sub)
{
	struct driver_data *data = dev->data;

	switch (id) {
	case PWRGRAPH_STATE:
		value_sub_manage(&data->sub, cb, sub);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static const struct value_driver_api power_graph_api = {
	.get = power_graph_get,
	.set = power_graph_set,
	.sub = power_graph_sub,
};

#define SPEC_NAME(inst, name) \
	UTIL_CAT(power_graph_spec_name_##inst##_, name)

#define SPEC_NAME_BY_IDX(inst, node_id, prop, idx) \
	SPEC_NAME(inst, DT_STRING_TOKEN_BY_IDX(node_id, prop, idx))

#define IS_STATE(node_id) DT_NODE_HAS_PROP(node_id, id)

#define CHECK_STATE(node_id)							   \
	IF_ENABLED(IS_STATE(node_id),						   \
		   (BUILD_ASSERT(DT_PROP(node_id, id) > 0,			   \
				 "State identifiers should be greater than zero"); \
		   ))

#define STATE_CONFIG(node_id)		    \
	IF_ENABLED(IS_STATE(node_id), ({    \
		.id = DT_PROP(node_id, id), \
	}, ))

#define IS_TRANSITION(node_id)			    \
	UTIL_OR(DT_NODE_HAS_PROP(node_id, initial), \
		DT_NODE_HAS_PROP(node_id, target))

#define STAGE_CONFIG(node_id, prop, idx, inst)		       \
	STAGE_PACK(SPEC_NAME_BY_IDX(inst, node_id, prop, idx), \
		   DT_PROP_BY_IDX(node_id, states, idx)),

#define STAGES_CONST_NAME(inst, node_id) \
	UTIL_CAT(power_graph_stages_##inst##_, DT_NODE_CHILD_IDX(node_id))

#define STAGES_CONFIG(node_id, inst)			       \
	IF_ENABLED(IS_TRANSITION(node_id),		       \
		   (static const stage_idx_t		       \
		    STAGES_CONST_NAME(inst, node_id)[] = {     \
		DT_FOREACH_PROP_ELEM_VARGS(node_id, stages,    \
					   STAGE_CONFIG, inst) \
	}; ))

#define OR_EQ_STATE_ID(node_id, state_id) \
	IF_ENABLED(IS_STATE(node_id), (|| DT_PROP(node_id, id) == (state_id)))

#define CHECK_STATE_ID(inst, state_id) \
	false DT_INST_FOREACH_CHILD_VARGS(inst, OR_EQ_STATE_ID, state_id)

/* TODO: Find solution to make this checks working */
#define CHECK_TRANSITION(node_id, inst)						  \
	IF_ENABLED(IS_TRANSITION(node_id),					  \
		   (BUILD_ASSERT(CHECK_STATE_ID(inst, DT_PROP(node_id, initial)), \
				 "Initial state identifier is not known");	  \
		    BUILD_ASSERT(CHECK_STATE_ID(inst, DT_PROP(node_id, target)),  \
				 "Target state identifier is not known"); ))
#undef CHECK_TRANSITION
#define CHECK_TRANSITION(node_id, inst)

#define TRANSITION_CONFIG(node_id, inst)			      \
	IF_ENABLED(IS_TRANSITION(node_id), ({			      \
		.initial = DT_PROP(node_id, initial),		      \
		.target = DT_PROP(node_id, target),		      \
		.stages = STAGES_CONST_NAME(inst, node_id),	      \
		.num_stages =					      \
			ARRAY_SIZE(STAGES_CONST_NAME(inst, node_id)), \
		.flags = DT_PROP(node_id, ignore_faults) ?	      \
			 power_transition_ignore_faults : 0,	      \
	}, ))

#define SUB_CB_DATA(node_id, prop, idx)	\
	VALUE_SUB_CB_INIT(power_graph_cb),

#define GRAPH_SPEC(node_id, prop, idx) \
	VALUE_DT_SPEC_GET_BY_IDX(node_id, prop, idx),

#define SPEC_NAME_ENTRY(node_id, prop, idx, inst) \
	SPEC_NAME_BY_IDX(inst, node_id, prop, idx),

#define POWER_GRAPH_DEVICE(inst)					 \
									 \
	enum power_graph_spec_names_##inst {				 \
		DT_INST_FOREACH_PROP_ELEM_VARGS(inst, value_names,	 \
						SPEC_NAME_ENTRY, inst)	 \
	};								 \
									 \
	DT_INST_FOREACH_CHILD(inst, CHECK_STATE);			 \
	DT_INST_FOREACH_CHILD_VARGS(inst, CHECK_TRANSITION, inst);	 \
									 \
	static const struct value_dt_spec power_graph_specs_##inst[] = { \
		DT_INST_FOREACH_PROP_ELEM(inst, values, GRAPH_SPEC)	 \
	};								 \
									 \
	static const struct power_state power_graph_states_##inst[] = {	 \
		DT_INST_FOREACH_CHILD(inst, STATE_CONFIG)		 \
	};								 \
									 \
	DT_INST_FOREACH_CHILD_VARGS(inst, STAGES_CONFIG, inst);		 \
									 \
	static const struct power_transition				 \
		power_graph_transitions_##inst[] = {			 \
		DT_INST_FOREACH_CHILD_VARGS(inst, TRANSITION_CONFIG,	 \
					    inst)			 \
	};								 \
									 \
	static const struct driver_config power_graph_config_##inst = {	 \
		.specs = power_graph_specs_##inst,			 \
		.num_specs = ARRAY_SIZE(power_graph_specs_##inst),	 \
		.states = power_graph_states_##inst,			 \
		.num_states = ARRAY_SIZE(power_graph_states_##inst),	 \
		.transitions = power_graph_transitions_##inst,		 \
		.num_transitions =					 \
			ARRAY_SIZE(power_graph_transitions_##inst),	 \
		.safe_state = DT_INST_PROP(inst, safe_state),		 \
	};								 \
									 \
	static struct driver_data power_graph_data_##inst = {		 \
		.dev = DEVICE_DT_GET(DT_DRV_INST(inst)),		 \
		.sub = VALUE_SUB_INIT(),				 \
		.state = DT_INST_PROP(inst, safe_state),		 \
		.new_state = DT_INST_PROP(inst, safe_state),		 \
		.transition = NO_STATE,					 \
		.stage = NO_STAGE,					 \
		.marker = {.func = NULL},				 \
		.cbs = { DT_INST_FOREACH_PROP_ELEM(inst, values,	 \
						   SUB_CB_DATA) },	 \
	};								 \
									 \
	DEVICE_DT_INST_DEFINE(inst, power_graph_init, NULL,		 \
			      &power_graph_data_##inst,			 \
			      &power_graph_config_##inst,		 \
			      POST_KERNEL,				 \
			      CONFIG_POWER_GRAPH_INIT_PRIORITY,		 \
			      &power_graph_api);

DT_INST_FOREACH_STATUS_OKAY(POWER_GRAPH_DEVICE)
