/**
 * @file mock_bus.h
 * @brief Mock hardware backend implementing the bus_interface_t.
 *
 * This is the "fake hardware" used for host-side testing and the demo. It owns
 * a small register file and reacts to command writes the way the real device
 * would (self-test, telemetry sampling, fault reset). It also exposes knobs to
 * inject failures so the driver's error paths can be exercised without any
 * physical hardware -- the same idea as a hardware-in-the-loop test stub.
 */
#ifndef SAT_MOCK_BUS_H
#define SAT_MOCK_BUS_H

#include <stdint.h>
#include <stdbool.h>
#include "bus.h"
#include "sat_sensor.h"

/** Number of addressable 8-bit registers in the mock device. */
#define MOCK_REG_COUNT 8u

/**
 * @brief Fault-injection configuration for the mock backend.
 *
 * Each field forces a specific abnormal condition so tests can validate the
 * driver's defensive behavior.
 */
typedef struct {
    bool    wrong_device_id;   /**< Report a bogus DEVICE_ID on read.        */
    bool    fail_reads;        /**< Every read_reg returns BUS_ERR.          */
    bool    fail_writes;       /**< Every write_reg returns BUS_ERR.         */
    bool    timeout_reads;     /**< Every read_reg returns BUS_TIMEOUT.      */
    bool    force_fault;       /**< Set FAULT_PRESENT and an error code.     */
    bool    invalid_telemetry; /**< Never set TELEMETRY_VALID after sample.  */
    uint8_t fault_error_code;  /**< Error code reported when force_fault set. */
} mock_fault_config_t;

/**
 * @brief Mock device state: register file + injected fault configuration.
 */
typedef struct {
    uint8_t             regs[MOCK_REG_COUNT];
    mock_fault_config_t faults;
} mock_device_t;

/**
 * @brief Reset the mock device to power-on defaults (valid ID, READY set).
 * @param dev Mock device storage to initialize.
 */
void mock_bus_reset(mock_device_t *dev);

/**
 * @brief Build a bus_interface_t bound to the given mock device.
 * @param dev Mock device that will back the returned interface.
 * @return A bus interface whose ctx points at @p dev.
 */
bus_interface_t mock_bus_create(mock_device_t *dev);

#endif /* SAT_MOCK_BUS_H */
