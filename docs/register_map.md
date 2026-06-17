# Register Map — Simulated Spacecraft Sensor

The device is modeled as an 8-register, 8-bit memory-mapped peripheral. Flight
software interacts with it exclusively through these registers, the same way a
real driver would access an I2C/SPI device or a memory-mapped IP core.

## Registers

| Addr | Name           | Access | Description                                   |
|------|----------------|--------|-----------------------------------------------|
| 0x00 | `DEVICE_ID`    | RO     | Fixed identifier, always `0x42`. Used to probe presence. |
| 0x01 | `STATUS`       | RO     | Status bitfield (see below).                  |
| 0x02 | `TEMP_HIGH`    | RO     | Temperature, high byte of 16-bit raw count.   |
| 0x03 | `TEMP_LOW`     | RO     | Temperature, low byte.                        |
| 0x04 | `VOLTAGE_HIGH` | RO     | Voltage, high byte of 16-bit raw count.       |
| 0x05 | `VOLTAGE_LOW`  | RO     | Voltage, low byte.                            |
| 0x06 | `COMMAND`      | WO     | Write a command opcode to trigger an action.  |
| 0x07 | `ERROR_CODE`   | RO     | Device-reported error code (0 when healthy).  |

## STATUS bits (0x01)

| Bit | Mask   | Name               | Meaning                                  |
|-----|--------|--------------------|------------------------------------------|
| 0   | `0x01` | `READY`            | Device is powered up and accepts commands. |
| 1   | `0x02` | `SELF_TEST_PASSED` | The most recent self-test passed.        |
| 2   | `0x04` | `TELEMETRY_VALID`  | Telemetry registers hold a valid frame.  |
| 3   | `0x08` | `FAULT_PRESENT`    | A fault is latched; see `ERROR_CODE`.    |

## Commands (write to 0x06)

| Opcode | Name               | Effect                                              |
|--------|--------------------|-----------------------------------------------------|
| `0xA0` | `SELF_TEST`        | Run BIST; sets `SELF_TEST_PASSED` on success.       |
| `0xB0` | `RESET_FAULTS`     | Clears `FAULT_PRESENT` and `ERROR_CODE`.            |
| `0xC0` | `SAMPLE_TELEMETRY` | Latches new temp/voltage; sets `TELEMETRY_VALID`.   |

## Telemetry scaling

Raw telemetry is a 16-bit unsigned count assembled as
`(HIGH << 8) | LOW`, then converted to engineering units:

- **Temperature:** `temperature_c = raw * 0.01`  → `0.01 °C` per LSB
- **Voltage:**     `voltage_v     = raw * 0.001` → `1 mV` per LSB

Nominal values produced by the mock device:

- Temperature raw `2500` → `25.00 °C`
- Voltage raw `3300` → `3.300 V`

## Typical command/poll sequence

```
write COMMAND = SAMPLE_TELEMETRY (0xC0)
loop (bounded):
    delay_ms(1)
    read STATUS
    if STATUS & FAULT_PRESENT  -> read ERROR_CODE, report fault
    if STATUS & TELEMETRY_VALID -> done
read TEMP_HIGH/LOW, VOLTAGE_HIGH/LOW, ERROR_CODE
```

The bounded poll loop is deliberate: flight software must never block forever
waiting on hardware, so the driver gives up after a fixed number of attempts and
returns `SAT_ERR_TIMEOUT`.
