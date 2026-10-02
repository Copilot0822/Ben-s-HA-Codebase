#pragma once

// DEVELOPMENT credentials: intentionally Espressif/CHIP standard test values.
// This header is consumed by the entire CHIP build via CONFIG_CHIP_PROJECT_CONFIG.
// VID/PID and provider choices are in sdkconfig.defaults.
// Change passcode AND its matching verifier together; see README.
#define CHIP_DEVICE_CONFIG_USE_TEST_SETUP_PIN_CODE 20202021
#define CHIP_DEVICE_CONFIG_USE_TEST_SETUP_DISCRIMINATOR 3840
#define CHIP_DEVICE_CONFIG_USE_TEST_SPAKE2P_ITERATION_COUNT 1000
#define CHIP_DEVICE_CONFIG_USE_TEST_SPAKE2P_SALT "U1BBS0UyUCBLZXkgU2FsdA=="
#define CHIP_DEVICE_CONFIG_USE_TEST_SPAKE2P_VERIFIER \
    "uWFwqugDNGiEck/po7KHwwMwwqZgN10XuyBajPGuyzUEV/iree4lOrao5GuwnlQ65CJzbeUB49s31EH+NEkg0JVI5MGCQGMMT/SRPFNRODm3wH/MBiehuFc6FJ/" \
    "NH6Rmzw=="
#define CHIP_DEVICE_CONFIG_DEVICE_VENDOR_NAME "Development"
#define CHIP_DEVICE_CONFIG_DEVICE_PRODUCT_NAME "SDP810 Thread Pressure"
