/**
 * @file sat_sensor.h
 * @brief Driver API for a simulated spacecraft sensor peripheral.
 *
 * The driver implements a small but realistic flight-software pattern:
 *   - probe/init the device by checking its DEVICE_ID,
 *   - issue commands by writing the COMMAND register,
 *   - poll the STATUS register for completion / fault bits,
 *   - read multi-byte telemetry (temperature, voltage),
 *   - surface faults and error codes back to the caller.
 *
 * All hardware access goes through a @ref bus_interface_t so the driver stays
 * transport-agnostic and unit-testable against a mock backend.
 */
#ifndef SAT_SENSOR_H
#define SAT_SENSOR_H

#include <stdint.h>
#include <stdbool.h>
#include "bus.h"

/* ------------------------------------------------------------------------- */
/* Register map (see docs/register_map.md)                                   */
/* ------------------------------------------------------------------------- */
#define SAT_REG_DEVICE_ID     0x00u  /**< RO: fixed device identifier.       */
#define SAT_REG_STATUS        0x01u  /**< RO: status bitfield.               */
#define SAT_REG_TEMP_HIGH     0x02u  /**< RO: temperature, high byte.        */
#define SAT_REG_TEMP_LOW      0x03u  /**< RO: temperature, low byte.         */
#define SAT_REG_VOLTAGE_HIGH  0x04u  /**< RO: voltage, high byte.            */
#define SAT_REG_VOLTAGE_LOW   0x05u  /**< RO: voltage, low byte.             */
#define SAT_REG_COMMAND       0x06u  /**< WO: command register.              */
#define SAT_REG_ERROR_CODE    0x07u  /**< RO: device-reported error code.    */

/* ------------------------------------------------------------------------- */
/* Status register bits                                                      */
/* ------------------------------------------------------------------------- */
#define SAT_STATUS_READY            (1u << 0) /**< Device ready for commands. */
#define SAT_STATUS_SELF_TEST_PASSED (1u << 1) /**< Last self-test passed.     */
#define SAT_STATUS_TELEMETRY_VALID  (1u << 2) /**< Telemetry regs are valid.  */
#define SAT_STATUS_FAULT_PRESENT    (1u << 3) /**< A fault is latched.        */

/* ------------------------------------------------------------------------- */
/* Command opcodes (written to SAT_REG_COMMAND)                              */
/* ------------------------------------------------------------------------- */
#define SAT_CMD_SELF_TEST        0xA0u /**< Run built-in self-test.          */
#define SAT_CMD_RESET_FAULTS     0xB0u /**< Clear latched faults/errors.     */
#define SAT_CMD_SAMPLE_TELEMETRY 0xC0u /**< Acquire a fresh telemetry frame. */

/* Expected identifier reported by SAT_REG_DEVICE_ID. */
#define SAT_DEVICE_ID 0x42u

/* Scaling: raw 16-bit counts -> engineering units. See docs/register_map.md. */
#define SAT_TEMP_SCALE_C_PER_LSB    0.01f /**< 0.01 degC per count.          */
#define SAT_VOLTAGE_SCALE_V_PER_LSB 0.001f /**< 1 mV per count.              */

/**
 * @brief Driver-level result codes.
 *
 * These are the contract between the driver and flight application code. Every
 * public function returns one of these so callers can branch on a single,
 * well-defined enum instead of guessing at magic numbers.
 */
typedef enum {
    SAT_OK = 0,                 /**< Operation succeeded.                     */
    SAT_ERR_NULL,               /**< A required pointer argument was NULL.    */
    SAT_ERR_BUS,                /**< Underlying bus transfer failed.          */
    SAT_ERR_DEVICE_ID,          /**< DEVICE_ID did not match expected value.  */
    SAT_ERR_TIMEOUT,            /**< Operation did not complete in time.      */
    SAT_ERR_FAULT,              /**< Device reported a fault condition.       */
    SAT_ERR_INVALID_TELEMETRY   /**< TELEMETRY_VALID not set after sampling.  */
} sat_status_t;

/**
 * @brief Decoded telemetry frame in engineering units.
 */
typedef struct {
    float    temperature_c; /**< Temperature in degrees Celsius. */
    float    voltage_v;     /**< Bus voltage in volts.           */
    uint8_t  status;        /**< Raw STATUS register snapshot.   */
    uint8_t  error_code;    /**< Raw ERROR_CODE register value.  */
} sat_telemetry_t;

/**
 * @brief Driver handle.
 *
 * Holds the bus binding plus a small amount of driver state. The caller owns
 * the storage; the driver does not allocate memory (important for flight code
 * where dynamic allocation is often disallowed).
 */
typedef struct {
    const bus_interface_t *bus; /**< Bus used for all register access.  */
    bool initialized;           /**< True once init() validated the ID. */
    uint8_t last_error_code;    /**< Cached ERROR_CODE from the device. */
} sat_sensor_t;

/**
 * @brief Bind a driver handle to a bus and verify the device is present.
 *
 * Reads DEVICE_ID and checks it against ::SAT_DEVICE_ID. On success the device
 * is marked initialized and ready for commands.
 *
 * @param dev Driver handle to initialize.
 * @param bus Bus interface the driver will use (must outlive @p dev).
 * @return SAT_OK, SAT_ERR_NULL, SAT_ERR_BUS/TIMEOUT, or SAT_ERR_DEVICE_ID.
 */
sat_status_t sat_sensor_init(sat_sensor_t *dev, const bus_interface_t *bus);

/**
 * @brief Read the raw DEVICE_ID register.
 */
sat_status_t sat_sensor_read_device_id(sat_sensor_t *dev, uint8_t *out_id);

/**
 * @brief Read the raw STATUS register.
 */
sat_status_t sat_sensor_read_status(sat_sensor_t *dev, uint8_t *out_status);

/**
 * @brief Command the device to run its built-in self-test and verify it passed.
 *
 * @return SAT_OK if SELF_TEST_PASSED is set, SAT_ERR_FAULT if a fault is
 *         latched, or a bus/timeout error.
 */
sat_status_t sat_sensor_run_self_test(sat_sensor_t *dev);

/**
 * @brief Command the device to acquire a fresh telemetry frame.
 *
 * After the command completes, TELEMETRY_VALID must be set; otherwise this
 * returns SAT_ERR_INVALID_TELEMETRY.
 */
sat_status_t sat_sensor_sample_telemetry(sat_sensor_t *dev);

/**
 * @brief Read and decode the latest telemetry into engineering units.
 *
 * Validates TELEMETRY_VALID and checks for a latched fault before returning.
 */
sat_status_t sat_sensor_read_telemetry(sat_sensor_t *dev, sat_telemetry_t *out);

/**
 * @brief Clear latched faults and the device error code.
 */
sat_status_t sat_sensor_reset_faults(sat_sensor_t *dev);

/**
 * @brief Return the last error code cached from the device.
 */
sat_status_t sat_sensor_get_error(sat_sensor_t *dev, uint8_t *out_error_code);

/**
 * @brief Human-readable string for a ::sat_status_t value (for logging/UI).
 */
const char *sat_status_str(sat_status_t status);

#endif /* SAT_SENSOR_H */
