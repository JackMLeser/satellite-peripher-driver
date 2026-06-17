/**
 * @file mock_bus.c
 * @brief Mock hardware backend behind the bus abstraction.
 *
 * Behavior modeled:
 *   - DEVICE_ID always reads back ::SAT_DEVICE_ID (0x42) unless overridden.
 *   - Writing SAT_CMD_SELF_TEST sets SELF_TEST_PASSED.
 *   - Writing SAT_CMD_SAMPLE_TELEMETRY loads temp/voltage and sets
 *     TELEMETRY_VALID.
 *   - Writing SAT_CMD_RESET_FAULTS clears FAULT_PRESENT and ERROR_CODE.
 * Fault-injection flags let tests force wrong IDs, bus errors, timeouts,
 * faults, and invalid telemetry.
 */
#include <stddef.h>

#include "mock_bus.h"

/* Nominal telemetry the device "samples" (raw 16-bit counts).
 * 2500 * 0.01 C  = 25.00 C
 * 3300 * 0.001 V =  3.300 V
 */
#define MOCK_TEMP_RAW    2500u
#define MOCK_VOLTAGE_RAW 3300u

static void apply_command(mock_device_t *dev, uint8_t cmd)
{
    switch (cmd) {
    case SAT_CMD_SELF_TEST:
        if (dev->faults.force_fault) {
            dev->regs[SAT_REG_STATUS]     |= SAT_STATUS_FAULT_PRESENT;
            dev->regs[SAT_REG_ERROR_CODE]  = dev->faults.fault_error_code;
        } else {
            dev->regs[SAT_REG_STATUS] |= SAT_STATUS_SELF_TEST_PASSED;
        }
        break;

    case SAT_CMD_SAMPLE_TELEMETRY:
        if (dev->faults.force_fault) {
            dev->regs[SAT_REG_STATUS]     |= SAT_STATUS_FAULT_PRESENT;
            dev->regs[SAT_REG_ERROR_CODE]  = dev->faults.fault_error_code;
            break;
        }
        dev->regs[SAT_REG_TEMP_HIGH]    = (uint8_t)(MOCK_TEMP_RAW >> 8);
        dev->regs[SAT_REG_TEMP_LOW]     = (uint8_t)(MOCK_TEMP_RAW & 0xFFu);
        dev->regs[SAT_REG_VOLTAGE_HIGH] = (uint8_t)(MOCK_VOLTAGE_RAW >> 8);
        dev->regs[SAT_REG_VOLTAGE_LOW]  = (uint8_t)(MOCK_VOLTAGE_RAW & 0xFFu);
        if (!dev->faults.invalid_telemetry) {
            dev->regs[SAT_REG_STATUS] |= SAT_STATUS_TELEMETRY_VALID;
        }
        break;

    case SAT_CMD_RESET_FAULTS:
        dev->regs[SAT_REG_STATUS]    &= (uint8_t)~SAT_STATUS_FAULT_PRESENT;
        dev->regs[SAT_REG_ERROR_CODE] = 0u;
        /* A successful reset implies the latched fault is gone; keep it gone
         * unless the test re-asserts force_fault on a later command. */
        dev->faults.force_fault = false;
        break;

    default:
        /* Unknown command: latch a fault so the driver can observe it. */
        dev->regs[SAT_REG_STATUS]    |= SAT_STATUS_FAULT_PRESENT;
        dev->regs[SAT_REG_ERROR_CODE] = 0xEEu;
        break;
    }
}

static bus_status_t mock_read_reg(void *ctx, uint8_t addr, uint8_t *value)
{
    mock_device_t *dev = (mock_device_t *)ctx;

    if (dev == NULL || value == NULL || addr >= MOCK_REG_COUNT) {
        return BUS_ERR;
    }
    if (dev->faults.timeout_reads) {
        return BUS_TIMEOUT;
    }
    if (dev->faults.fail_reads) {
        return BUS_ERR;
    }
    if (addr == SAT_REG_DEVICE_ID && dev->faults.wrong_device_id) {
        *value = 0xFFu; /* bogus id */
        return BUS_OK;
    }

    *value = dev->regs[addr];
    return BUS_OK;
}

static bus_status_t mock_write_reg(void *ctx, uint8_t addr, uint8_t value)
{
    mock_device_t *dev = (mock_device_t *)ctx;

    if (dev == NULL || addr >= MOCK_REG_COUNT) {
        return BUS_ERR;
    }
    if (dev->faults.fail_writes) {
        return BUS_ERR;
    }

    if (addr == SAT_REG_COMMAND) {
        dev->regs[SAT_REG_COMMAND] = value;
        apply_command(dev, value);
    } else {
        dev->regs[addr] = value;
    }
    return BUS_OK;
}

static void mock_delay_ms(void *ctx, uint32_t ms)
{
    (void)ctx;
    (void)ms; /* No real time passes in the host-side mock. */
}

void mock_bus_reset(mock_device_t *dev)
{
    if (dev == NULL) {
        return;
    }
    for (unsigned i = 0; i < MOCK_REG_COUNT; ++i) {
        dev->regs[i] = 0u;
    }
    dev->regs[SAT_REG_DEVICE_ID] = SAT_DEVICE_ID;
    dev->regs[SAT_REG_STATUS]    = SAT_STATUS_READY;

    dev->faults.wrong_device_id   = false;
    dev->faults.fail_reads        = false;
    dev->faults.fail_writes       = false;
    dev->faults.timeout_reads     = false;
    dev->faults.force_fault       = false;
    dev->faults.invalid_telemetry = false;
    dev->faults.fault_error_code  = 0u;
}

bus_interface_t mock_bus_create(mock_device_t *dev)
{
    bus_interface_t bus;
    bus.ctx       = dev;
    bus.read_reg  = mock_read_reg;
    bus.write_reg = mock_write_reg;
    bus.delay_ms  = mock_delay_ms;
    return bus;
}
