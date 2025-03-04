/**
 * @file
 * @brief API for power graphs.
 */

#ifndef ZEPHYR_INCLUDE_DT_BINDINGS_POWER_GRAPH_H_
#define ZEPHYR_INCLUDE_DT_BINDINGS_POWER_GRAPH_H_

/**
 * @brief Power Graph Device-Tree constants
 * @defgroup power_graph_dt Power Graph Interface
 * @ingroup device_tree
 * @{
 */

#define PWRGRAPH_DT_COMPAT power_graph

/**
 * @brief State of graph
 *
 * Graph can be turned on or off by setting this value.
 * Set @p true to turn it on, set @p false to turn it off.
 *
 * Current status of graph can be given by getting this value.
 */
#define PWRGRAPH_STATE 0

/**
 * @brief Number of faults
 */
#define PWRGRAPH_NUM_FAULTS 1

#define PWRGRAPH_FAULT_DATA_TRANSITION 0x1000
#define PWRGRAPH_FAULT_DATA_STAGE 0x2000
#define PWRGRAPH_FAULT_DATA_SPEC 0x4000
#define PWRGRAPH_FAULT_DATA_MASK 0x7000
#define PWRGRAPH_FAULT_DATA(id) ((id) &PWRGRAPH_FAULT_DATA_MASK)
#define PWRGRAPH_FAULT_DEPTH(id) ((id) &~PWRGRAPH_FAULT_DATA_MASK)

/**
 * @brief N-th fault transition
 */
#define PWRGRAPH_FAULT_TRANSITION(n) (PWRGRAPH_FAULT_DATA_TRANSITION + (n))

/**
 * @brief N-th fault stage
 */
#define PWRGRAPH_FAULT_STAGE(n) (PWRGRAPH_FAULT_DATA_STAGE + (n))

/**
 * @brief N-th fault spec
 */
#define PWRGRAPH_FAULT_SPEC(n) (PWRGRAPH_FAULT_DATA_SPEC + (n))

/**
 * @}
 */

#endif /* ZEPHYR_INCLUDE_DT_BINDINGS_POWER_GRAPH_H_ */
