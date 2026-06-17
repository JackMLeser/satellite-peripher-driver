# sat-peripheral-driver-demo

A small, interview-ready C driver for a **simulated spacecraft sensor
peripheral**. It demonstrates the core mechanics of embedded/flight software
talking to hardware: register-level I/O, device initialization, command writes,
telemetry acquisition, status/fault handling, timeout/error handling, and
test-driven validation against a mock hardware backend.

> This project is **not** a production flight driver. It is a focused
> embedded-software demo showing how flight software can interact with a
> lower-level peripheral through a register map and bus abstraction. The driver
> handles initialization, command writes, telemetry acquisition, status bits,
> fault handling, and test-driven validation using a mock hardware backend.

---

## 1. What this project is

A self-contained C project that implements a device driver for a fictional
8-register spacecraft sensor (temperature + voltage telemetry). The driver
never touches real hardware directly — it goes through a small **bus
abstraction** (`read_reg` / `write_reg` / `delay_ms`). For testing and the demo,
a **mock hardware backend** implements that bus and can inject faults on demand.

## 2. Why it is relevant to satellite flight software

Satellite flight software spends much of its life reading sensors and
controlling peripherals over buses like I2C, SPI, UART, CAN, or SpaceWire.
Almost all of that interaction reduces to the same pattern this project models:

- probe a device and confirm its identity,
- write a command register to trigger an action,
- poll a status register (with a bounded timeout) for completion or fault,
- read back multi-byte telemetry and convert it to engineering units,
- detect, report, and recover from faults — without ever hanging the task.

## 3. Device-driver concepts it demonstrates

- **Register map abstraction** — named registers, status bitfields, command opcodes.
- **Hardware abstraction layer (HAL)** — driver depends on function pointers, not a transport.
- **Initialization / probe** — device ID check before use.
- **Command + status polling** — bounded-wait loop, no infinite spins.
- **Telemetry decode** — 16-bit raw counts → Celsius / Volts via fixed scaling.
- **Fault handling** — fault bit detection, error-code latching, reset/recovery.
- **Robust error contract** — every call returns a single `sat_status_t` enum.
- **No dynamic allocation** — caller owns the handle (flight-friendly).
- **Test-driven validation** — fault injection covers the failure paths.

## 4. How the register map works

| Addr | Name           | Access | Purpose                          |
|------|----------------|--------|----------------------------------|
| 0x00 | `DEVICE_ID`    | RO     | Fixed `0x42`, used to probe.      |
| 0x01 | `STATUS`       | RO     | Status bitfield.                 |
| 0x02 | `TEMP_HIGH`    | RO     | Temperature high byte.           |
| 0x03 | `TEMP_LOW`     | RO     | Temperature low byte.            |
| 0x04 | `VOLTAGE_HIGH` | RO     | Voltage high byte.               |
| 0x05 | `VOLTAGE_LOW`  | RO     | Voltage low byte.                |
| 0x06 | `COMMAND`      | WO     | Command opcode register.         |
| 0x07 | `ERROR_CODE`   | RO     | Device error code.               |

STATUS bits: `READY (0x01)`, `SELF_TEST_PASSED (0x02)`,
`TELEMETRY_VALID (0x04)`, `FAULT_PRESENT (0x08)`.

Commands: `SELF_TEST (0xA0)`, `RESET_FAULTS (0xB0)`, `SAMPLE_TELEMETRY (0xC0)`.

Telemetry scaling: `temperature_c = raw * 0.01`, `voltage_v = raw * 0.001`.

Full details: [`docs/register_map.md`](docs/register_map.md).

## 5. How the bus abstraction works

The driver only ever calls three function pointers defined in
[`include/bus.h`](include/bus.h):

```c
typedef struct bus_interface {
    void *ctx;
    bus_status_t (*read_reg)(void *ctx, uint8_t addr, uint8_t *value);
    bus_status_t (*write_reg)(void *ctx, uint8_t addr, uint8_t value);
    void         (*delay_ms)(void *ctx, uint32_t ms);
} bus_interface_t;
```

Because the driver depends on this interface (not a concrete transport), the
exact same driver code could later be backed by an I2C, SPI, UART, CAN, or
SpaceWire implementation — only the backend changes. Here the backend is
`src/mock_bus.c`, a register file in memory with fault-injection switches.
See [`docs/architecture.md`](docs/architecture.md) for the layer diagram.

## 6. How to build and run

Requires CMake (≥ 3.10) and a C11 compiler.

```bash
mkdir build
cd build
cmake ..
make
./sat_demo
./sat_tests
```

Optional: run the host-side validation script (configures, builds, runs tests,
and checks the demo output — a mini CI / functional-validation harness):

```bash
python3 tools/run_checks.py
```

You can also run the tests through CTest: `cd build && ctest --output-on-failure`.

## 7. Example output

### Full terminal session (copy-paste)

```console
$ mkdir build && cd build
$ cmake .. && make
-- Configuring done
-- Generating done
[ 14%] Building C object CMakeFiles/sat_driver.dir/src/sat_sensor.c.o
[ 28%] Building C object CMakeFiles/sat_driver.dir/src/mock_bus.c.o
[ 42%] Linking C static library libsat_driver.a
[ 57%] Building C object CMakeFiles/sat_demo.dir/src/main.c.o
[ 71%] Linking C executable sat_demo
[ 85%] Building C object CMakeFiles/sat_tests.dir/tests/test_sat_sensor.c.o
[100%] Linking C executable sat_tests

$ ./sat_demo
=== sat-peripheral-driver-demo ===

[init]        SAT_OK
              DEVICE_ID = 0x42
[self_test]   SAT_OK
[sample]      SAT_OK
[telemetry]   SAT_OK
    temperature : 25.00 C
    voltage     : 3.300 V
    status      : 0x07
    error_code  : 0x00

--- fault handling demo ---
[self_test]   SAT_ERR_FAULT (fault injected)
              ERROR_CODE = 0x17
[reset]       SAT_OK
[self_test]   SAT_OK (after recovery)

Done.

$ ./sat_tests
=== sat_sensor unit tests ===
- test_init_success
    ok
... (all 9 tests) ...
- test_invalid_telemetry
    ok

37 checks, 0 failure(s)
$ echo $?
0
```

### `./sat_demo` (annotated)

```
=== sat-peripheral-driver-demo ===

[init]        SAT_OK
              DEVICE_ID = 0x42
[self_test]   SAT_OK
[sample]      SAT_OK
[telemetry]   SAT_OK
    temperature : 25.00 C
    voltage     : 3.300 V
    status      : 0x07
    error_code  : 0x00

--- fault handling demo ---
[self_test]   SAT_ERR_FAULT (fault injected)
              ERROR_CODE = 0x17
[reset]       SAT_OK
[self_test]   SAT_OK (after recovery)

Done.
```

`./sat_tests`:

```
=== sat_sensor unit tests ===
- test_init_success
    ok
- test_init_wrong_id
    ok
- test_self_test_sets_bit
    ok
- test_telemetry_values
    ok
- test_fault_detected
    ok
- test_reset_clears_fault
    ok
- test_bus_read_failure
    ok
- test_null_guards
    ok
- test_invalid_telemetry
    ok

37 checks, 0 failure(s)
```

## 8. Interview talking points

- I built this after seeing **device driver development** and **hardware/software
  interfaces** in the Airbus role description.
- I wanted to understand how **flight software talks to peripherals through a
  register-level interface**.
- The C driver is **separated from the bus backend**, which is similar to how
  embedded drivers are separated from I2C/SPI/UART/CAN implementations.
- The **mock backend** lets me validate normal *and* failure behavior without
  physical hardware — the software analog of hardware-in-the-loop testing.
- The project gave me better language for discussing **BSPs, device interfaces,
  HIL testing, and flight software validation**.

### Likely follow-up questions I can speak to

- *Why poll with a bounded loop instead of `while(1)`?* Flight tasks must not
  block forever on hardware; a timeout returns control and reports `SAT_ERR_TIMEOUT`.
- *Why function pointers for the bus?* Portability + testability; the driver is
  transport-agnostic and the BSP supplies the transport.
- *Why no `malloc`?* The caller owns the handle; deterministic memory use is
  preferred in flight software.
- *How would you take this to real hardware?* Implement `read_reg`/`write_reg`/
  `delay_ms` against a real peripheral and pass that `bus_interface_t` to the
  driver — nothing in `sat_sensor.c` changes.

---

## What I would add next

A deliberately scoped roadmap that shows how this demo would grow toward real
flight-software practice — each item reuses the existing `bus_interface_t` seam:

- **Real UART/SPI/CAN backend** — implement `read_reg`/`write_reg`/`delay_ms`
  against an actual MCU peripheral (e.g. STM32 HAL or a Linux `spidev`/`i2c-dev`
  handle). The driver and tests stay untouched; only a new bus file is added.
- **RTOS task wrapper** — wrap the driver in a periodic FreeRTOS/RTEMS task that
  samples telemetry on a fixed cadence, publishes it to a queue, and guards
  shared access with a mutex — demonstrating concurrency and timing under an RTOS.
- **Hardware-in-the-loop (HIL) test target** — add a CMake target that runs the
  same test suite against real hardware (or a bus-level simulator) so the exact
  validation logic covers both host and target.
- **Fault injection campaign** — drive the mock's fault knobs from a data table
  to systematically sweep every error path and report coverage, instead of
  hand-written individual cases.
- **Timing / latency measurements** — instrument command-to-completion latency
  (poll count, worst-case `delay_ms` budget) and assert bounded execution time —
  relevant to deterministic flight timing requirements.
- **CI test runner** — wire `tools/run_checks.py` into GitHub Actions (or
  GitLab CI) to build, run unit tests, and validate demo output on every push,
  optionally with `-Werror` and sanitizers (ASan/UBSan).

---

## Project layout

```
sat-peripheral-driver-demo/
  README.md
  CMakeLists.txt
  include/
    sat_sensor.h      # driver API, register map, status codes
    bus.h             # generic bus abstraction (HAL)
    mock_bus.h        # mock backend API + fault-injection config
  src/
    sat_sensor.c      # driver implementation
    mock_bus.c        # fake hardware backend
    main.c            # demo application
  tests/
    test_sat_sensor.c # unit tests (no external framework)
  tools/
    run_checks.py     # build + test + demo validation script
  docs/
    register_map.md   # register/bit/command reference
    architecture.md   # layer diagram + design rationale
```
