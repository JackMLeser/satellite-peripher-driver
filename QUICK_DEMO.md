# Quick Demo

Everything you need to build, run, and talk through the project in a few minutes.

## 1. Build

```bash
cd sat-peripheral-driver-demo   # must be inside this folder, not the parent
mkdir -p build
cd build
cmake ..
make
```

## 2. Run the demo

```bash
./sat_demo
```

## 3. Run the tests

```bash
./sat_tests
```

(Optional) one-shot build + test + demo validation from the project root:

```bash
python3 tools/run_checks.py
```

## 4. Expected output

`./sat_demo`:

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

`./sat_tests` ends with:

```
37 checks, 0 failure(s)
```

## 5. Interview script (follow this live)

1. **Frame it (15s).** "This is a small C driver for a simulated spacecraft
   sensor. It shows how flight software talks to a peripheral through a register
   map and a bus abstraction."

2. **Build + run (30s).** Run the four build commands, then `./sat_demo`. Point
   at the output: "Init checks the DEVICE_ID, self-test passes, then it samples
   telemetry — 25 °C, 3.3 V — decoded from raw registers."

3. **Show fault handling (20s).** Point at the fault section: "I inject a fault,
   the driver reports `SAT_ERR_FAULT` and latches the error code, then
   `reset_faults` clears it and the device works again."

4. **Show the abstraction (30s).** Open `include/bus.h`: "The driver only uses
   these three function pointers — `read_reg`, `write_reg`, `delay_ms`. Swap the
   backend for I2C/SPI/UART/CAN and the driver doesn't change. That's the
   driver/BSP separation."

5. **Show the tests (20s).** Run `./sat_tests`: "The mock backend injects wrong
   IDs, bus errors, timeouts, and faults, so I validate every error path with no
   hardware — like hardware-in-the-loop testing in software."

6. **Close (15s).** "It's intentionally small and not production flight code, but
   it gave me concrete language for BSPs, device interfaces, bounded polling,
   fault handling, and on-host validation." (See `INTERVIEW_NOTES.md` for more.)
