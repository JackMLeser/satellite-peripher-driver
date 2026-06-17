# Interview Notes — sat-peripheral-driver-demo

A cheat sheet for talking about this project clearly and honestly.

## 60-second explanation

I built a small C device driver for a simulated spacecraft sensor. The driver
does what real flight software does with a peripheral: it probes the device by
checking its ID, writes command registers, polls a status register with a
bounded timeout, reads back telemetry (temperature and voltage), and handles
faults. The key design choice is that the driver talks to hardware only through
a tiny bus abstraction — three function pointers: `read_reg`, `write_reg`,
`delay_ms`. That means the same driver could sit on I2C, SPI, UART, CAN, or
SpaceWire later. For testing, a mock backend implements that bus in memory and
can inject faults, so I validate both the normal path and every failure path
without any hardware.

## 2-minute technical explanation

The project has four layers: a demo app, the driver, a bus abstraction (HAL),
and a backend behind it.

- **Register map.** The device is modeled as eight 8-bit registers: DEVICE_ID,
  STATUS, TEMP high/low, VOLTAGE high/low, COMMAND, ERROR_CODE. STATUS is a
  bitfield (READY, SELF_TEST_PASSED, TELEMETRY_VALID, FAULT_PRESENT). Commands
  are opcodes written to COMMAND (SELF_TEST, RESET_FAULTS, SAMPLE_TELEMETRY).

- **Driver.** Each operation follows the classic pattern: write a command, then
  poll STATUS in a *bounded* loop until the expected bit appears, failing fast if
  FAULT_PRESENT shows up. Two commands (self-test, sample) share one internal
  `issue_command()` helper. Telemetry is assembled from two bytes into a 16-bit
  count and scaled to engineering units (0.01 °C/LSB, 1 mV/LSB). Every public
  function returns a single `sat_status_t` enum, so callers branch on one
  well-defined contract.

- **Bus abstraction.** The driver never calls a transport directly — only the
  three function pointers in `bus.h`. This is the BSP separation: chip logic in
  the driver, board/transport logic behind the bus.

- **Mock backend.** `mock_bus.c` is a register file plus fault-injection
  switches (wrong ID, read/write failure, timeout, forced fault, invalid
  telemetry). The unit tests flip those switches to drive the driver's error
  paths — the software analog of hardware-in-the-loop testing.

It builds with CMake, produces `sat_demo` and `sat_tests`, and the tests run
with no external framework.

## Why this maps to spacecraft flight software

Flight software spends much of its time reading sensors and commanding
peripherals over serial buses. That work reduces to the exact pattern here:
probe, command, poll-with-timeout, read telemetry, detect and recover from
faults — all without blocking the task forever or allocating memory dynamically.
The driver/BSP split mirrors how real flight code is structured so it can be
ported across boards and validated off-target before it ever flies.

## Device-driver concepts it demonstrates

- Register-level I/O and a status/command register protocol.
- Hardware abstraction layer (function-pointer bus) and BSP separation.
- Device probe/identification on init.
- Bounded polling with a timeout (no infinite hardware waits).
- Fault detection, error-code latching, and reset/recovery.
- Telemetry decoding (byte assembly + fixed-point scaling).
- A single, explicit error-code contract.
- No dynamic allocation (caller-owned handle).
- Test-driven validation via a mock backend and fault injection.

## What I would improve if I had more time

- A real UART/SPI/CAN backend implementing the same bus interface.
- An RTOS task wrapper that samples telemetry periodically.
- A hardware-in-the-loop target that runs the same tests on real hardware.
- A table-driven fault-injection campaign with coverage reporting.
- Command-to-completion latency/timing measurements.
- A CI runner (build + tests + sanitizers) on every push.

## Honest limitations

- The "hardware" is a mock register file, not a real bus or device.
- Registers are 8-bit and the protocol is intentionally simplified.
- Timing is modeled with a no-op delay; there is no real-time scheduling.
- No concurrency/locking — single-threaded by design for this demo.
- No CRC/parity, retries, or power-state management that a flight driver needs.
- Telemetry scaling is fixed and uncalibrated; it is illustrative only.

This is a focused learning/demo project, not production flight code — and I can
speak to exactly what would change to make it flight-worthy.
