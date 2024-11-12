/*
 * Copyright (c) 2023 MBT
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Simple value I/O driver APIs
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_VALUE_H_
#define ZEPHYR_INCLUDE_DRIVERS_VALUE_H_

/**
 * @brief Value I/O Interface
 * @defgroup value_interface Value I/O Interface
 * @ingroup io_interfaces
 * @{
 */

#include <stdint.h>
#include <errno.h>
#include <zephyr/types.h>
#include <zephyr/device.h>
#include <zephyr/sys/slist.h>
#include <zephyr/sys/__assert.h>
#include <errno.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @typedef value_id_t
 * @brief The type of identifier to get and/or set values
 *
 */
typedef uint32_t value_id_t;

/**
 * @typedef value_t
 * @brief The type of value to get and/or set
 *
 */
typedef int32_t value_t;

/**
 * @brief Minimum value
 */
#define VALUE_MIN INT32_MIN

/**
 * @brief Maximum value
 */
#define VALUE_MAX INT32_MAX

/**
 * @brief Value reference device-tree spec
 */
struct value_dt_spec {
	const struct device *dev;
	value_id_t id;
};

/**
 * @brief Static initializer for a @p value_dt_spec
 */
#define VALUE_DT_SPEC_GET_BY_IDX(node_id, prop, idx)			     \
	{								     \
		.dev = DEVICE_DT_GET(DT_PHANDLE_BY_IDX(node_id, prop, idx)), \
		.id = DT_PHA_BY_IDX(node_id, prop, idx, value_id),	     \
	}

/**
 * @brief Static initializer for a @p value_dt_spec from a DT_DRV_COMPAT
 * instance's value property at an index.
 *
 * @see VALUE_DT_SPEC_GET_BY_IDX()
 */
#define VALUE_DT_SPEC_INST_GET_BY_IDX(inst, prop, idx) \
	VALUE_DT_SPEC_GET_BY_IDX(DT_DRV_INST(inst), prop, idx)

/**
 * @typedef value_api_get()
 * @brief Callback API for getting output value by identifier
 *
 * @see value_get() for argument descriptions.
 */
typedef int (*value_api_get)(const struct device *dev,
			     value_id_t id,
			     value_t *pval);

/**
 * @typedef value_api_set()
 * @brief Callback API for setting input value by identifier
 *
 * @see value_set() for argument descriptions.
 */
typedef int (*value_api_set)(const struct device *dev,
			     value_id_t id,
			     value_t val);

struct value_sub_cb;

/**
 * @typedef value_sub_fn()
 * @brief Callback function for watching value changes
 */
typedef void (*value_sub_fn)(struct value_sub_cb *cb,
			     const struct device *dev,
			     value_id_t id);

/**
 * @typedef value_sub_cb()
 * @brief Callback for subscribing to value changes
 */
struct value_sub_cb {
	sys_snode_t node;
	value_sub_fn func;
};

/**
 * @brief Statically initialize subscription callback
 */
#define VALUE_SUB_CB_INIT(func_) { .func = (func_), .node = { .next = NULL } }

/**
 * @brief Statically define subscription callback
 */
#define VALUE_SUB_CB_DEFINE(name_)			 \
	static void name_##_fn(struct value_sub_cb *cb,	 \
			       const struct device *dev, \
			       value_id_t id);		 \
	struct value_sub_cb name_ =			 \
		VALUE_SUB_CB_INIT(name_##_fn);		 \
	static void name_##_fn(struct value_sub_cb *cb,	 \
			       const struct device *dev, \
			       value_id_t id)

/**
 * @brief Initialize callback with function
 */
static inline void value_sub_cb_init(struct value_sub_cb *cb,
				     value_sub_fn func)
{
	cb->node.next = NULL;
	cb->func = func;
}

/**
 * @brief Check callback subscription status
 *
 * @param cb A pointer to the callback
 */
static inline bool value_sub_active(struct value_sub_cb *cb)
{
	return cb->node.next != NULL;
}

/**
 * @brief Value subscriptions list
 */
struct value_sub {
	sys_slist_t list;
};

/**
 * @brief Initialize subscriptions list
 *
 * @param subs A pointer to the list to initialize
 */
static inline void value_sub_init(struct value_sub *sub)
{
	sys_slist_init(&sub->list);
}

/**
 * @brief Static initialize subscriptions list
 */
#define VALUE_SUB_INIT() { .list = SYS_SLIST_STATIC_INIT() }

/**
 * @brief Subscribe/unsubscribe to/from value changes
 */
static inline void value_sub_manage(struct value_sub *sub,
				    struct value_sub_cb *cb,
				    bool on)
{
	__ASSERT(value_sub_active(cb) != on, "Attempt to %s",
		 on ? "subscribe twice" : "unsubscribe which hasn't subscribed");
	if (on) {
		sys_slist_append(&sub->list, &cb->node);
	} else {
		(void)sys_slist_find_and_remove(&sub->list, &cb->node);
	}
}

/**
 * @brief Notify subscribers
 */
static inline void value_sub_notify(struct value_sub *sub,
				    const struct device *dev,
				    value_id_t id)
{
	sys_snode_t *sn, *sns;

	SYS_SLIST_FOR_EACH_NODE_SAFE(&sub->list, sn, sns) {
		struct value_sub_cb *cb =
			CONTAINER_OF(sn, struct value_sub_cb, node);

		cb->func(cb, dev, id);
	}
}

/**
 * @typedef value_api_sub()
 * @brief Callback API for subscribing to value changes
 *
 * @see value_sub() for argument descriptions.
 */
typedef int (*value_api_sub)(const struct device *dev,
			     value_id_t id,
			     struct value_sub_cb *cb,
			     bool on);

/**
 * @brief Value driver API
 */
__subsystem struct value_driver_api {
	/* Optional callbacks. */
	value_api_get get;
	value_api_set set;
	value_api_sub sub;
};

/**
 * @brief Get output value
 *
 * This optional routine gets the output values by identifiers.
 *
 * @param dev Output device
 * @param id Output identifier
 * @param pval Pointer to value to get
 * @return 0 on success, negative on error
 */
__syscall int value_get(const struct device *dev,
			value_id_t id,
			value_t *pval);

static inline int z_impl_value_get(const struct device *dev,
				   value_id_t id,
				   value_t *pval)
{
	const struct value_driver_api *api =
		(const struct value_driver_api *)dev->api;

	if (api->get == NULL) {
		return -ENOSYS;
	}
	return api->get(dev, id, pval);
}

/**
 * @brief Get output value
 *
 * This optional routine gets the output values by identifiers.
 *
 * @param spec Value specifier from device-tree
 * @param pval Pointer to value to get
 * @return 0 on success, negative on error
 */
static inline int value_get_dt(const struct value_dt_spec *spec,
			       value_t *pval)
{
	return value_get(spec->dev, spec->id, pval);
}

/**
 * @brief Set input value
 *
 * This optional routine sets the input values by identifiers.
 *
 * @param dev Input device
 * @param id Input identifier
 * @param val Actual value to set
 * @return 0 on success, negative on error
 */
__syscall int value_set(const struct device *dev,
			value_id_t id,
			value_t val);

static inline int z_impl_value_set(const struct device *dev,
				   value_id_t id,
				   value_t val)
{
	const struct value_driver_api *api =
		(const struct value_driver_api *)dev->api;

	if (api->set == NULL) {
		return -ENOSYS;
	}
	return api->set(dev, id, val);
}

/**
 * @brief Set input value
 *
 * This optional routine sets the input values by identifiers.
 *
 * @param spec Value specifier from device-tree
 * @param val Actual value to set
 * @return 0 on success, negative on error
 */
static inline int value_set_dt(const struct value_dt_spec *spec,
			       value_t val)
{
	return value_set(spec->dev, spec->id, val);
}

/**
 * @brief Subscribe to value changes
 *
 * This optional routine allows subscribe to value changes.
 *
 * @param dev Input device
 * @param id Value identifier to subscribe
 * @param cb Callback to subscribe/unsubscribe
 * @param on Set true to subscribe and false to unsubscribe
 * @return 0 on success, negative on error
 */
__syscall int value_sub(const struct device *dev,
			value_id_t id,
			struct value_sub_cb *cb,
			bool on);

static inline int z_impl_value_sub(const struct device *dev,
				   value_id_t id,
				   struct value_sub_cb *cb,
				   bool on)
{
	const struct value_driver_api *api =
		(const struct value_driver_api *)dev->api;

	if (api->sub == NULL) {
		return -ENOSYS;
	}
	return api->sub(dev, id, cb, on);
}

/**
 * @brief Subscribe to value changes
 *
 * This optional routine allows subscribe to value changes.
 *
 * @param spec Value specifier from device-tree
 * @param cb Callback to subscribe/unsubscribe
 * @param on Set true to subscribe and false to unsubscribe
 * @return 0 on success, negative on error
 */
static inline int value_sub_dt(const struct value_dt_spec *spec,
			       struct value_sub_cb *cb,
			       bool on)
{
	return value_sub(spec->dev, spec->id, cb, on);
}

#ifdef __cplusplus
}
#endif

/**
 * @}
 */

#include <syscalls/value.h>

#endif /* ZEPHYR_INCLUDE_DRIVERS_VALUE_H_ */
