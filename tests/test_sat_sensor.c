/**
 * @file test_sat_sensor.c
 * @brief Lightweight unit tests for the sat_sensor driver.
 *
 * Uses a tiny assertion macro instead of a heavyweight framework so the test
 * binary stays dependency-free and easy to read in an interview. Each test
 * configures the mock backend, exercises one driver behavior, and checks the
 * returned ::sat_status_t and any output values.
 */
#include <stdio.h>
#include <string.h>

#include "sat_sensor.h"
#include "mock_bus.h"

/* --- minimal test harness ------------------------------------------------ */

static int g_checks = 0;
static int g_failures = 0;

#define CHECK(cond)                                                            \
    do {                                                                      \
        ++g_checks;                                                          \
        if (!(cond)) {                                                       \
            ++g_failures;                                                    \
            printf("  [FAIL] %s:%d: %s\n", __FILE__, __LINE__, #cond);       \
        }                                                                   \
    } while (0)

#define RUN_TEST(fn)                                                          \
    do {                                                                      \
        int before = g_failures;                                            \
        printf("- %s\n", #fn);                                              \
        fn();                                                               \
        printf("    %s\n", (g_failures == before) ? "ok" : "FAILED");       \
    } while (0)

/* Helper: fresh device + bound driver for each test. */
static void setup(mock_device_t *hw, bus_interface_t *bus)
{
    mock_bus_reset(hw);
    *bus = mock_bus_create(hw);
}

/* --- test cases ---------------------------------------------------------- */

/* 1. init succeeds with valid device ID. */
static void test_init_success(void)
{
    mock_device_t hw; bus_interface_t bus; setup(&hw, &bus);
    sat_sensor_t dev;

    CHECK(sat_sensor_init(&dev, &bus) == SAT_OK);
    CHECK(dev.initialized == true);

    uint8_t id = 0;
    CHECK(sat_sensor_read_device_id(&dev, &id) == SAT_OK);
    CHECK(id == SAT_DEVICE_ID);
}

/* 2. init fails with wrong device ID. */
static void test_init_wrong_id(void)
{
    mock_device_t hw; bus_interface_t bus; setup(&hw, &bus);
    hw.faults.wrong_device_id = true;
    sat_sensor_t dev;

    CHECK(sat_sensor_init(&dev, &bus) == SAT_ERR_DEVICE_ID);
    CHECK(dev.initialized == false);
}

/* 3. self-test sets the expected status bit. */
static void test_self_test_sets_bit(void)
{
    mock_device_t hw; bus_interface_t bus; setup(&hw, &bus);
    sat_sensor_t dev;
    CHECK(sat_sensor_init(&dev, &bus) == SAT_OK);

    CHECK(sat_sensor_run_self_test(&dev) == SAT_OK);

    uint8_t status = 0;
    CHECK(sat_sensor_read_status(&dev, &status) == SAT_OK);
    CHECK((status & SAT_STATUS_SELF_TEST_PASSED) != 0);
}

/* 4. telemetry read returns expected temperature and voltage. */
static void test_telemetry_values(void)
{
    mock_device_t hw; bus_interface_t bus; setup(&hw, &bus);
    sat_sensor_t dev;
    CHECK(sat_sensor_init(&dev, &bus) == SAT_OK);

    CHECK(sat_sensor_sample_telemetry(&dev) == SAT_OK);

    sat_telemetry_t tlm;
    memset(&tlm, 0, sizeof(tlm));
    CHECK(sat_sensor_read_telemetry(&dev, &tlm) == SAT_OK);

    /* 2500 * 0.01 = 25.00 C, 3300 * 0.001 = 3.300 V (see mock_bus.c). */
    CHECK(tlm.temperature_c > 24.99f && tlm.temperature_c < 25.01f);
    CHECK(tlm.voltage_v > 3.299f && tlm.voltage_v < 3.301f);
    CHECK((tlm.status & SAT_STATUS_TELEMETRY_VALID) != 0);
}

/* 5. fault condition is detected. */
static void test_fault_detected(void)
{
    mock_device_t hw; bus_interface_t bus; setup(&hw, &bus);
    sat_sensor_t dev;
    CHECK(sat_sensor_init(&dev, &bus) == SAT_OK);

    hw.faults.force_fault      = true;
    hw.faults.fault_error_code = 0x17;

    CHECK(sat_sensor_run_self_test(&dev) == SAT_ERR_FAULT);

    uint8_t err = 0;
    CHECK(sat_sensor_get_error(&dev, &err) == SAT_OK);
    CHECK(err == 0x17);
}

/* 6. reset faults clears the fault. */
static void test_reset_clears_fault(void)
{
    mock_device_t hw; bus_interface_t bus; setup(&hw, &bus);
    sat_sensor_t dev;
    CHECK(sat_sensor_init(&dev, &bus) == SAT_OK);

    hw.faults.force_fault      = true;
    hw.faults.fault_error_code = 0x17;
    CHECK(sat_sensor_run_self_test(&dev) == SAT_ERR_FAULT);

    CHECK(sat_sensor_reset_faults(&dev) == SAT_OK);

    uint8_t status = 0;
    CHECK(sat_sensor_read_status(&dev, &status) == SAT_OK);
    CHECK((status & SAT_STATUS_FAULT_PRESENT) == 0);

    uint8_t err = 0xFF;
    CHECK(sat_sensor_get_error(&dev, &err) == SAT_OK);
    CHECK(err == 0x00);

    /* Device should be usable again after recovery. */
    CHECK(sat_sensor_run_self_test(&dev) == SAT_OK);
}

/* 7. bus read failure returns an error code instead of crashing. */
static void test_bus_read_failure(void)
{
    mock_device_t hw; bus_interface_t bus; setup(&hw, &bus);
    sat_sensor_t dev;

    /* Read failure during init -> SAT_ERR_BUS. */
    hw.faults.fail_reads = true;
    CHECK(sat_sensor_init(&dev, &bus) == SAT_ERR_BUS);

    /* Timeout flavor also maps cleanly. */
    setup(&hw, &bus);
    hw.faults.timeout_reads = true;
    CHECK(sat_sensor_init(&dev, &bus) == SAT_ERR_TIMEOUT);
}

/* Bonus: NULL-argument guarding. */
static void test_null_guards(void)
{
    mock_device_t hw; bus_interface_t bus; setup(&hw, &bus);
    sat_sensor_t dev;

    CHECK(sat_sensor_init(NULL, &bus) == SAT_ERR_NULL);
    CHECK(sat_sensor_init(&dev, NULL) == SAT_ERR_NULL);
    CHECK(sat_sensor_init(&dev, &bus) == SAT_OK);
    CHECK(sat_sensor_read_device_id(&dev, NULL) == SAT_ERR_NULL);
}

/* 8. invalid telemetry is rejected. */
static void test_invalid_telemetry(void)
{
    mock_device_t hw; bus_interface_t bus; setup(&hw, &bus);
    sat_sensor_t dev;
    CHECK(sat_sensor_init(&dev, &bus) == SAT_OK);

    hw.faults.invalid_telemetry = true;
    /* Sampling never sets TELEMETRY_VALID -> command times out... */
    CHECK(sat_sensor_sample_telemetry(&dev) == SAT_ERR_TIMEOUT);
    /* ...and a direct read reports invalid telemetry. */
    sat_telemetry_t tlm;
    CHECK(sat_sensor_read_telemetry(&dev, &tlm) == SAT_ERR_INVALID_TELEMETRY);
}

int main(void)
{
    printf("=== sat_sensor unit tests ===\n");

    RUN_TEST(test_init_success);
    RUN_TEST(test_init_wrong_id);
    RUN_TEST(test_self_test_sets_bit);
    RUN_TEST(test_telemetry_values);
    RUN_TEST(test_fault_detected);
    RUN_TEST(test_reset_clears_fault);
    RUN_TEST(test_bus_read_failure);
    RUN_TEST(test_null_guards);
    RUN_TEST(test_invalid_telemetry);

    printf("\n%d checks, %d failure(s)\n", g_checks, g_failures);
    return (g_failures == 0) ? 0 : 1;
}
