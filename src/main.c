/**
 * @file main.c
 * @brief Demo application: bring up the sensor, self-test, read telemetry.
 *
 * This mimics a tiny slice of flight software startup: probe the device,
 * confirm it is healthy via self-test, acquire a telemetry frame, and print
 * decoded engineering values. It then demonstrates fault detection and
 * recovery using the mock backend's fault-injection hooks.
 */
#include <stdio.h>

#include "sat_sensor.h"
#include "mock_bus.h"

static void print_telemetry(const sat_telemetry_t *tlm)
{
    printf("    temperature : %.2f C\n", (double)tlm->temperature_c);
    printf("    voltage     : %.3f V\n", (double)tlm->voltage_v);
    printf("    status      : 0x%02X\n", tlm->status);
    printf("    error_code  : 0x%02X\n", tlm->error_code);
}

int main(void)
{
    mock_device_t hw;
    mock_bus_reset(&hw);
    bus_interface_t bus = mock_bus_create(&hw);

    sat_sensor_t dev;
    sat_status_t st;

    printf("=== sat-peripheral-driver-demo ===\n\n");

    /* 1. Initialize / probe the device. */
    st = sat_sensor_init(&dev, &bus);
    printf("[init]        %s\n", sat_status_str(st));
    if (st != SAT_OK) {
        return 1;
    }

    uint8_t id = 0;
    (void)sat_sensor_read_device_id(&dev, &id);
    printf("              DEVICE_ID = 0x%02X\n", id);

    /* 2. Built-in self-test. */
    st = sat_sensor_run_self_test(&dev);
    printf("[self_test]   %s\n", sat_status_str(st));

    /* 3. Acquire and read telemetry. */
    st = sat_sensor_sample_telemetry(&dev);
    printf("[sample]      %s\n", sat_status_str(st));

    sat_telemetry_t tlm;
    st = sat_sensor_read_telemetry(&dev, &tlm);
    printf("[telemetry]   %s\n", sat_status_str(st));
    if (st == SAT_OK) {
        print_telemetry(&tlm);
    }

    /* 4. Inject a fault, detect it, then recover. */
    printf("\n--- fault handling demo ---\n");
    hw.faults.force_fault      = true;
    hw.faults.fault_error_code = 0x17;

    st = sat_sensor_run_self_test(&dev);
    printf("[self_test]   %s (fault injected)\n", sat_status_str(st));

    uint8_t err = 0;
    (void)sat_sensor_get_error(&dev, &err);
    printf("              ERROR_CODE = 0x%02X\n", err);

    st = sat_sensor_reset_faults(&dev);
    printf("[reset]       %s\n", sat_status_str(st));

    st = sat_sensor_run_self_test(&dev);
    printf("[self_test]   %s (after recovery)\n", sat_status_str(st));

    printf("\nDone.\n");
    return 0;
}
