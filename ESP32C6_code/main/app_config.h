#pragma once

#include <cstddef>
#include <cstdint>

// GPIO numbers, NOT physical header pin numbers. Change wiring here.
namespace app_config {
constexpr int kSdaGpio = 6;
constexpr int kSclGpio = 7;
constexpr int kResetGpio = 9; // DevKit BOOT button, active low. Hold AFTER boot.
constexpr uint8_t kSensorAddress = 0x25;
constexpr uint32_t kI2cClockHz = 100000;
constexpr int kI2cTimeoutMs = 50;
constexpr uint32_t kSampleMs = 100;
constexpr size_t kFilterSamples = 5; // 0.5 s window, approximately 0.2 s group delay.
constexpr uint32_t kPublishMs = 1000;
constexpr uint32_t kRetryMs = 5000;
constexpr uint32_t kStaleMs = 3000;
constexpr uint32_t kDebugMs = 10000;
constexpr uint32_t kStatusMs = 30000;
constexpr uint32_t kFactoryResetHoldMs = 5000;
constexpr uint32_t kOtaConfirmUptimeMs = 10000;
constexpr float kPressureSign = 1.0f; // Set to -1 ONLY to deliberately reverse port polarity.
static_assert(kPressureSign == 1.0f || kPressureSign == -1.0f);
static_assert(kSdaGpio != kSclGpio && kSdaGpio != kResetGpio && kSclGpio != kResetGpio);
static_assert(kFilterSamples > 0 && kSampleMs > 0);
} // namespace app_config
