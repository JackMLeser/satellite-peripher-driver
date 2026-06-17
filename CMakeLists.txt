cmake_minimum_required(VERSION 3.10)
project(sat_peripheral_driver_demo C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Treat warnings seriously: flight-style code should compile clean.
if(NOT MSVC)
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

include_directories(${CMAKE_SOURCE_DIR}/include)

# Driver + mock backend as a small static library so both the demo app and the
# test binary link against the exact same driver code.
add_library(sat_driver STATIC
    src/sat_sensor.c
    src/mock_bus.c
)
target_include_directories(sat_driver PUBLIC ${CMAKE_SOURCE_DIR}/include)

# Demo application.
add_executable(sat_demo src/main.c)
target_link_libraries(sat_demo PRIVATE sat_driver)

# Unit tests.
add_executable(sat_tests tests/test_sat_sensor.c)
target_link_libraries(sat_tests PRIVATE sat_driver)

# Allow `ctest` to run the test binary too.
enable_testing()
add_test(NAME sat_tests COMMAND sat_tests)
