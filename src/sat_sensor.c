/**
 * @file sat_sensor.c
 * @brief Implementation of the simulated spacecraft sensor driver.
 *
 * The driver follows a classic memory-mapped peripheral pattern:
 *   write a command -> wait -> poll status -> read result.
 * Every register access is funneled through small helpers that translate raw
 * bus status codes into the driver's ::sat_status_t contract.
 */
#include <stddef.h>

#include "sat_sensor.h"

/*
 * Number of status polls we will perform while waiting for a command to
 * complete before declaring a timeout. Combined with the bus delay_ms() this
 * models the bounded-wait behavior expected of flight software (no infinite
 * loops waiting on hardware).
 */
#define SAT_POLL_RETRIES 10u
#define SAT_POLL_DELAY_MS 1u

/* --- internal helpers ---------------------------------------------------- */

/** Translate a bus_status_t into the driver-level sat_status_t. */
static sat_status_t map_bus_status(bus_status_t bs)
{
    switch (bs) {
    case BUS_OK:      return SAT_OK;
    case BUS_TIMEOUT: return SAT_ERR_TIMEOUT;
    case BUS_ERR:     /* fallthrough */
    default:          return SAT_ERR_BUS;
    }
}

/** Validate that a handle is non-NULL and bound to a usable bus. */
static sat_status_t check_handle(const sat_sensor_t *dev)
{
    if (dev == NULL || dev->bus == NULL ||
        dev->bus->read_reg == NULL || dev->bus->write_reg == NULL) {
        return SAT_ERR_NULL;
    }
    return SAT_OK;
}

/** Read a single register through the bus, mapping errors to sat_status_t. */
static sat_status_t read_reg(sat_sensor_t *dev, uint8_t addr, uint8_t *value)
{
    bus_status_t bs = dev->bus->read_reg(dev->bus->ctx, addr, value);
    return map_bus_status(bs);
}

/** Write a single register through the bus, mapping errors to sat_status_t. */
static sat_status_t write_reg(sat_sensor_t *dev, uint8_t addr, uint8_t value)
{
    bus_status_t bs = dev->bus->write_reg(dev->bus->ctx, addr, value);
    return map_bus_status(bs);
}

/** Optional cooperative delay; no-op if the backend does not provide one. */
static void bus_delay(sat_sensor_t *dev, uint32_t ms)
{
    if (dev->bus->delay_ms != NULL) {
        dev->bus->delay_ms(dev->bus->ctx, ms);
    }
}

/**
 * @brief Issue a command and poll STATUS until @p wait_bits are all set.
 *
 * Caches ERROR_CODE and fails fast on FAULT_PRESENT so a faulted device does
 * not spin until timeout.
 *
 * @return SAT_OK once all @p wait_bits are set; SAT_ERR_FAULT, SAT_ERR_TIMEOUT,
 *         or a bus error otherwise.
 */
static sat_status_t issue_command(sat_sensor_t *dev, uint8_t cmd, uint8_t wait_bits)
{
    sat_status_t st = write_reg(dev, SAT_REG_COMMAND, cmd);
    if (st != SAT_OK) {
        return st;
    }

    for (uint32_t attempt = 0; attempt < SAT_POLL_RETRIES; ++attempt) {
        bus_delay(dev, SAT_POLL_DELAY_MS);

        uint8_t status = 0;
        st = read_reg(dev, SAT_REG_STATUS, &status);
        if (st != SAT_OK) {
            return st;
        }

        if (status & SAT_STATUS_FAULT_PRESENT) {
            /* Latch the device error code so callers can inspect it. */
            (void)read_reg(dev, SAT_REG_ERROR_CODE, &dev->last_error_code);
            return SAT_ERR_FAULT;
        }

        if ((status & wait_bits) == wait_bits) {
            return SAT_OK;
        }
    }

    return SAT_ERR_TIMEOUT;
}

/* --- public API ---------------------------------------------------------- */

sat_status_t sat_sensor_init(sat_sensor_t *dev, const bus_interface_t *bus)
{
    if (dev == NULL || bus == NULL ||
        bus->read_reg == NULL || bus->write_reg == NULL) {
        return SAT_ERR_NULL;
    }

    dev->bus = bus;
    dev->initialized = false;
    dev->last_error_code = 0;

    uint8_t id = 0;
    sat_status_t st = read_reg(dev, SAT_REG_DEVICE_ID, &id);
    if (st != SAT_OK) {
        return st;
    }
    if (id != SAT_DEVICE_ID) {
        return SAT_ERR_DEVICE_ID;
    }

    dev->initialized = true;
    return SAT_OK;
}

sat_status_t sat_sensor_read_device_id(sat_sensor_t *dev, uint8_t *out_id)
{
    sat_status_t st = check_handle(dev);
    if (st != SAT_OK) {
        return st;
    }
    if (out_id == NULL) {
        return SAT_ERR_NULL;
    }
    return read_reg(dev, SAT_REG_DEVICE_ID, out_id);
}

sat_status_t sat_sensor_read_status(sat_sensor_t *dev, uint8_t *out_status)
{
    sat_status_t st = check_handle(dev);
    if (st != SAT_OK) {
        return st;
    }
    if (out_status == NULL) {
        return SAT_ERR_NULL;
    }
    return read_reg(dev, SAT_REG_STATUS, out_status);
}

sat_status_t sat_sensor_run_self_test(sat_sensor_t *dev)
{
    sat_status_t st = check_handle(dev);
    if (st != SAT_OK) {
        return st;
    }
    return issue_command(dev, SAT_CMD_SELF_TEST, SAT_STATUS_SELF_TEST_PASSED);
}

sat_status_t sat_sensor_sample_telemetry(sat_sensor_t *dev)
{
    sat_status_t st = check_handle(dev);
    if (st != SAT_OK) {
        return st;
    }
    return issue_command(dev, SAT_CMD_SAMPLE_TELEMETRY, SAT_STATUS_TELEMETRY_VALID);
}

sat_status_t sat_sensor_read_telemetry(sat_sensor_t *dev, sat_telemetry_t *out)
{
    sat_status_t st = check_handle(dev);
    if (st != SAT_OK) {
        return st;
    }
    if (out == NULL) {
        return SAT_ERR_NULL;
    }

    uint8_t status = 0;
    st = read_reg(dev, SAT_REG_STATUS, &status);
    if (st != SAT_OK) {
        return st;
    }

    if (status & SAT_STATUS_FAULT_PRESENT) {
        (void)read_reg(dev, SAT_REG_ERROR_CODE, &dev->last_error_code);
        return SAT_ERR_FAULT;
    }
    if (!(status & SAT_STATUS_TELEMETRY_VALID)) {
        return SAT_ERR_INVALID_TELEMETRY;
    }

    uint8_t t_hi = 0, t_lo = 0, v_hi = 0, v_lo = 0, err = 0;
    if ((st = read_reg(dev, SAT_REG_TEMP_HIGH, &t_hi))    != SAT_OK) return st;
    if ((st = read_reg(dev, SAT_REG_TEMP_LOW, &t_lo))     != SAT_OK) return st;
    if ((st = read_reg(dev, SAT_REG_VOLTAGE_HIGH, &v_hi)) != SAT_OK) return st;
    if ((st = read_reg(dev, SAT_REG_VOLTAGE_LOW, &v_lo))  != SAT_OK) return st;
    if ((st = read_reg(dev, SAT_REG_ERROR_CODE, &err))    != SAT_OK) return st;

    /* Telemetry decode: peripherals expose multi-byte values as separate 8-bit
     * registers, so we reassemble the high/low bytes into a 16-bit count
     * (big-endian here) and then apply the device's fixed scaling to convert
     * raw counts into engineering units (degrees C and volts). */
    uint16_t temp_raw = (uint16_t)(((uint16_t)t_hi << 8) | t_lo);
    uint16_t volt_raw = (uint16_t)(((uint16_t)v_hi << 8) | v_lo);

    out->temperature_c = (float)temp_raw * SAT_TEMP_SCALE_C_PER_LSB;
    out->voltage_v     = (float)volt_raw * SAT_VOLTAGE_SCALE_V_PER_LSB;
    out->status        = status;
    out->error_code    = err;

    dev->last_error_code = err;
    return SAT_OK;
}

sat_status_t sat_sensor_reset_faults(sat_sensor_t *dev)
{
    sat_status_t st = check_handle(dev);
    if (st != SAT_OK) {
        return st;
    }

    st = write_reg(dev, SAT_REG_COMMAND, SAT_CMD_RESET_FAULTS);
    if (st != SAT_OK) {
        return st;
    }
    bus_delay(dev, SAT_POLL_DELAY_MS);

    /* Confirm the fault bit actually cleared. */
    uint8_t status = 0;
    st = read_reg(dev, SAT_REG_STATUS, &status);
    if (st != SAT_OK) {
        return st;
    }
    if (status & SAT_STATUS_FAULT_PRESENT) {
        (void)read_reg(dev, SAT_REG_ERROR_CODE, &dev->last_error_code);
        return SAT_ERR_FAULT;
    }

    dev->last_error_code = 0;
    return SAT_OK;
}

sat_status_t sat_sensor_get_error(sat_sensor_t *dev, uint8_t *out_error_code)
{
    sat_status_t st = check_handle(dev);
    if (st != SAT_OK) {
        return st;
    }
    if (out_error_code == NULL) {
        return SAT_ERR_NULL;
    }

    /* Refresh from the device, but always return the cached value on failure. */
    uint8_t err = dev->last_error_code;
    if (read_reg(dev, SAT_REG_ERROR_CODE, &err) == SAT_OK) {
        dev->last_error_code = err;
    }
    *out_error_code = dev->last_error_code;
    return SAT_OK;
}

const char *sat_status_str(sat_status_t status)
{
    switch (status) {
    case SAT_OK:                    return "SAT_OK";
    case SAT_ERR_NULL:              return "SAT_ERR_NULL";
    case SAT_ERR_BUS:               return "SAT_ERR_BUS";
    case SAT_ERR_DEVICE_ID:         return "SAT_ERR_DEVICE_ID";
    case SAT_ERR_TIMEOUT:           return "SAT_ERR_TIMEOUT";
    case SAT_ERR_FAULT:             return "SAT_ERR_FAULT";
    case SAT_ERR_INVALID_TELEMETRY: return "SAT_ERR_INVALID_TELEMETRY";
    default:                        return "SAT_ERR_UNKNOWN";
    }
}
