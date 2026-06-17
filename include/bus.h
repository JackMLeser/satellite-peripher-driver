/**
 * @file bus.h
 * @brief Generic register-level bus abstraction for the satellite peripheral.
 *
 * WHY THIS LAYER EXISTS
 * ---------------------
 * This interface is the "hardware abstraction layer" (HAL) that the driver
 * talks through. The driver (sat_sensor.c) deliberately knows NOTHING about how
 * bytes actually reach the device. It only knows "read register N", "write
 * register N", and "wait N milliseconds". Everything transport-specific lives
 * behind these three function pointers.
 *
 * This separation buys four things that matter for flight software:
 *
 *   1. Portability / BSP separation. The same driver can run on top of an I2C,
 *      SPI, UART, CAN, SpaceWire, or memory-mapped backend. Porting to a new
 *      board means writing a new bus implementation (a board support package
 *      concern), not editing the driver. Chip logic and board logic stay apart.
 *
 *   2. Testability without hardware. A mock backend (mock_bus.c) implements
 *      this same interface entirely in memory, so the driver's normal AND
 *      failure paths can be exercised on a host machine. This is the software
 *      stand-in for hardware-in-the-loop (HIL) testing.
 *
 *   3. Fault injection. Because every transfer flows through one choke point,
 *      the backend can deterministically force NACKs, timeouts, and bad data to
 *      validate the driver's error handling.
 *
 *   4. A clear contract. Transfers return a small, explicit status
 *      (::bus_status_t) instead of ad-hoc error numbers, so the driver can map
 *      hardware-level failures onto its own ::sat_status_t cleanly.
 */
#ifndef SAT_BUS_H
#define SAT_BUS_H

#include <stdint.h>

/**
 * @brief Status codes returned by bus transactions.
 *
 * Bus operations are kept deliberately small: a transfer either completes
 * (BUS_OK) or it fails in a way the driver can reason about (NACK/timeout).
 */
typedef enum {
    BUS_OK = 0,      /**< Transfer completed successfully.            */
    BUS_ERR = 1,     /**< Generic bus failure (e.g. device NACK).     */
    BUS_TIMEOUT = 2  /**< Transfer did not complete in time.          */
} bus_status_t;

/**
 * @brief Bus interface exposed to the driver via function pointers.
 *
 * Using function pointers (instead of calling a concrete backend directly)
 * lets us swap the underlying transport and inject faults for testing without
 * touching the driver. @c ctx is an opaque pointer to backend-private state.
 */
typedef struct bus_interface {
    /** Opaque backend context (e.g. mock register file or a real HW handle). */
    void *ctx;

    /**
     * @brief Read one 8-bit register.
     * @param ctx   Backend context.
     * @param addr  Register address.
     * @param value Out: register value on success.
     * @return BUS_OK on success, otherwise an error/timeout code.
     */
    bus_status_t (*read_reg)(void *ctx, uint8_t addr, uint8_t *value);

    /**
     * @brief Write one 8-bit register.
     * @param ctx   Backend context.
     * @param addr  Register address.
     * @param value Value to write.
     * @return BUS_OK on success, otherwise an error/timeout code.
     */
    bus_status_t (*write_reg)(void *ctx, uint8_t addr, uint8_t value);

    /**
     * @brief Busy/blocking delay used to model command execution time.
     * @param ctx Backend context.
     * @param ms  Milliseconds to delay.
     */
    void (*delay_ms)(void *ctx, uint32_t ms);
} bus_interface_t;

#endif /* SAT_BUS_H */
