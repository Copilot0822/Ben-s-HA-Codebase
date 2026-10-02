#include "app_config.h"
#include "pressure_math.h"
#include "sdp810.h"
#include "thread_config.h"

#include <atomic>
#include <cinttypes>
#include <driver/gpio.h>
#include <esp_app_desc.h>
#include <esp_log.h>
#include <esp_matter.h>
#include <esp_ota_ops.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <nvs_flash.h>
#include <app/server/CommissioningWindowManager.h>
#include <app/server/Server.h>
#include <platform/CHIPDeviceLayer.h>
#include <platform/CommissionableDataProvider.h>
#include <platform/ESP32/OpenthreadLauncher.h>
#include <setup_payload/OnboardingCodesUtil.h>

#if !CHIP_DEVICE_CONFIG_ENABLE_THREAD || CHIP_DEVICE_CONFIG_ENABLE_WIFI_STATION
#error "This firmware requires Matter over Thread with Wi-Fi station disabled"
#endif

namespace {
constexpr char kTag[] = "pressure_app";
uint16_t pressure_endpoint_id;
Sdp810 sensor;
pressure::Reading raw_reading; // Latest unfiltered, signed, physical Pa for debugging.
pressure::MovingAverage<app_config::kFilterSamples> filter;
float filtered_pa = 0;
bool sensor_running = false;
int64_t last_good_ms = -1;
std::atomic<bool> publish_pending{false};
std::atomic<int32_t> latest_value{INT16_MIN}; // Reserved Matter null sentinel.

int64_t now_ms() { return esp_timer_get_time() / 1000; }

// All Matter attribute, fabric and connectivity access runs on the CHIP thread.
void log_status(intptr_t)
{
    auto &connectivity = chip::DeviceLayer::ConnectivityMgr();
    const auto fabrics = chip::Server::GetInstance().GetFabricTable().FabricCount();
    const bool window = chip::Server::GetInstance().GetCommissioningWindowManager().IsCommissioningWindowOpen();
    ESP_LOGI(kTag, "Thread: dataset=%s attached=%s; commissioning: fabrics=%u window=%s",
             connectivity.IsThreadProvisioned() ? "stored" : "absent",
             connectivity.IsThreadAttached() ? "yes" : "no", static_cast<unsigned>(fabrics),
             window ? "open" : "closed");
}

void log_onboarding(intptr_t)
{
    uint32_t passcode = 0;
    uint16_t discriminator = 0;
    auto *provider = chip::DeviceLayer::GetCommissionableDataProvider();
    if (provider->GetSetupPasscode(passcode) == CHIP_NO_ERROR &&
        provider->GetSetupDiscriminator(discriminator) == CHIP_NO_ERROR) {
        ESP_LOGI(kTag, "DEVELOPMENT setup passcode: %08" PRIu32 "; discriminator: %u", passcode, discriminator);
    }
    const chip::RendezvousInformationFlags flags(chip::RendezvousInformationFlag::kBLE);
    char qr[128]{};
    chip::MutableCharSpan qr_span(qr, sizeof(qr));
    char manual[32]{};
    chip::MutableCharSpan manual_span(manual, sizeof(manual));
    CHIP_ERROR err = GetQRCode(qr_span, flags);
    if (err == CHIP_NO_ERROR) ESP_LOGI(kTag, "Matter QR payload: %s", qr);
    else ESP_LOGE(kTag, "QR payload failed: %" CHIP_ERROR_FORMAT, err.Format());
    err = GetManualPairingCode(manual_span, flags);
    if (err == CHIP_NO_ERROR) ESP_LOGI(kTag, "Manual pairing code: %s", manual);
    else ESP_LOGE(kTag, "Manual pairing code failed: %" CHIP_ERROR_FORMAT, err.Format());
    log_status(0);
}

void publish_on_chip_thread(intptr_t)
{
    const int32_t value = latest_value.load();
    esp_matter_attr_val_t attribute_value = value == INT16_MIN
        ? esp_matter_nullable_int16(nullable<int16_t>())
        : esp_matter_nullable_int16(static_cast<int16_t>(value));
    const esp_err_t err = esp_matter::attribute::update(pressure_endpoint_id,
        chip::app::Clusters::PressureMeasurement::Id,
        chip::app::Clusters::PressureMeasurement::Attributes::MeasuredValue::Id, &attribute_value);
    if (err != ESP_OK) ESP_LOGW(kTag, "Matter pressure update failed: %s; retry next second", esp_err_to_name(err));
    publish_pending.store(false);
}

void queue_pressure(int32_t value)
{
    latest_value.store(value);
    // Coalesce if the CHIP event loop is temporarily busy; never grow an unbounded queue.
    if (publish_pending.exchange(true)) return;
    CHIP_ERROR err = chip::DeviceLayer::PlatformMgr().ScheduleWork(publish_on_chip_thread);
    if (err != CHIP_NO_ERROR) {
        publish_pending.store(false);
        ESP_LOGW(kTag, "Matter work queue busy: %" CHIP_ERROR_FORMAT, err.Format());
    }
}

void reset_on_chip_thread(intptr_t)
{
    ESP_LOGW(kTag, "Factory reset: erasing Matter fabrics and Thread dataset, then rebooting");
    esp_err_t err = esp_matter::factory_reset();
    if (err != ESP_OK) ESP_LOGE(kTag, "Factory reset returned: %s", esp_err_to_name(err));
}

void reset_button_task(void *)
{
    int64_t pressed_since = -1;
    bool armed = false;
    bool reset_sent = false;
    for (;;) {
        if (gpio_get_level(static_cast<gpio_num_t>(app_config::kResetGpio)) != 0) {
            if (armed && !reset_sent) {
                CHIP_ERROR err = chip::DeviceLayer::PlatformMgr().ScheduleWork(reset_on_chip_thread);
                reset_sent = err == CHIP_NO_ERROR;
                if (!reset_sent) ESP_LOGE(kTag, "Could not queue factory reset");
            }
            pressed_since = -1;
            armed = false;
        } else if (!reset_sent) {
            if (pressed_since < 0) pressed_since = now_ms();
            if (!armed && now_ms() - pressed_since >= app_config::kFactoryResetHoldMs) {
                armed = true;
                ESP_LOGW(kTag, "Factory reset armed; release BOOT button to reset");
            }
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

bool initialize_sensor()
{
    esp_err_t err = sensor.start(raw_reading);
    ESP_LOGI(kTag, "SDP810 initialization: %s (I2C 0x%02x SDA=%d SCL=%d)",
        esp_err_to_name(err), app_config::kSensorAddress, app_config::kSdaGpio, app_config::kSclGpio);
    if (err != ESP_OK) return false;
    ESP_LOGI(kTag, "SDP810 scale factor: %" PRId32 " counts/Pa (125 Pa model normally 240)", raw_reading.scale_factor);
    filter.reset();
    filtered_pa = filter.add(raw_reading.pressure_pa * app_config::kPressureSign);
    last_good_ms = now_ms();
    return true;
}

void sensor_task(void *)
{
    const int64_t task_started = now_ms();
    bool ota_checked = false;
    int64_t last_retry = now_ms();
    int64_t last_publish = 0, last_debug = 0, last_status = 0, last_error = -5000;
    unsigned failures = 0;
    bool stale_filter_cleared = false;
    TickType_t wake = xTaskGetTickCount();
    for (;;) {
        const int64_t now = now_ms();
        if (!ota_checked && now - task_started >= app_config::kOtaConfirmUptimeMs) {
            // Health criterion: Matter start succeeded, and the sampling task ran
            // for 10 s. A missing external sensor or border router is not a bad image.
            // A boot crash/reset before confirmation permits bootloader rollback.
            ota_checked = true;
            esp_ota_img_states_t state;
            const esp_partition_t *running = esp_ota_get_running_partition();
            if (running && esp_ota_get_state_partition(running, &state) == ESP_OK &&
                state == ESP_OTA_IMG_PENDING_VERIFY) {
                const esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
                ESP_LOGI(kTag, "OTA boot health confirmation: %s", esp_err_to_name(err));
                if (err != ESP_OK) ota_checked = false; // Retry instead of silently committing a failed check.
            }
        }
        if (!sensor_running && now - last_retry >= app_config::kRetryMs) {
            last_retry = now;
            sensor_running = initialize_sensor();
            failures = 0;
        } else if (sensor_running) {
            esp_err_t err = sensor.read(raw_reading);
            if (err == ESP_OK) {
                filtered_pa = filter.add(raw_reading.pressure_pa * app_config::kPressureSign);
                last_good_ms = now_ms();
                failures = 0;
            } else {
                ++failures;
                if (now - last_error >= app_config::kRetryMs) {
                    last_error = now;
                    ESP_LOGW(kTag, "SDP810 read failed: %s; consecutive=%u", esp_err_to_name(err), failures);
                }
                if (failures >= 10) {
                    sensor_running = false;
                    last_retry = now;
                }
            }
        }
        const bool fresh = last_good_ms >= 0 && now_ms() - last_good_ms < app_config::kStaleMs;
        if (!fresh && !stale_filter_cleared) {
            filter.reset(); // Never blend old samples into recovered measurements.
            stale_filter_cleared = true;
        } else if (fresh) {
            stale_filter_cleared = false;
        }
        if (now - last_publish >= app_config::kPublishMs) {
            last_publish = now;
            queue_pressure(fresh ? pressure::to_matter(filtered_pa) : INT16_MIN);
        }
        if (now - last_debug >= app_config::kDebugMs) {
            last_debug = now;
            if (fresh) {
                ESP_LOGI(kTag, "Raw SDP810: %.2f Pa | Filtered: %.2f Pa | Matter MeasuredValue: %d%s",
                    static_cast<double>(raw_reading.pressure_pa), static_cast<double>(filtered_pa),
                    pressure::to_matter(filtered_pa), std::fabs(filtered_pa) > 125 ? " (range clamped)" : "");
            } else {
                ESP_LOGW(kTag, "SDP810 unavailable/stale; Matter MeasuredValue: null; retrying");
            }
        }
        if (now - last_status >= app_config::kStatusMs) {
            last_status = now;
            (void)chip::DeviceLayer::PlatformMgr().ScheduleWork(log_status);
        }
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(app_config::kSampleMs));
    }
}

void matter_event(const chip::DeviceLayer::ChipDeviceEvent *event, intptr_t)
{
    using namespace chip::DeviceLayer;
    switch (event->Type) {
    case DeviceEventType::kCommissioningComplete:
        ESP_LOGI(kTag, "Matter commissioning complete"); break;
    case DeviceEventType::kCommissioningWindowOpened:
        ESP_LOGI(kTag, "Matter commissioning window opened (BLE)"); break;
    case DeviceEventType::kCommissioningWindowClosed:
        ESP_LOGI(kTag, "Matter commissioning window closed"); break;
    case DeviceEventType::kCommissioningSessionStarted:
        ESP_LOGI(kTag, "Matter commissioning session started"); break;
    case DeviceEventType::kFailSafeTimerExpired:
        ESP_LOGW(kTag, "Matter commissioning fail-safe expired; check commissioner and Thread credentials"); break;
    case DeviceEventType::kThreadConnectivityChange:
    case DeviceEventType::kThreadStateChange:
    case DeviceEventType::kInterfaceIpAddressChanged:
    case DeviceEventType::kFabricRemoved:
        log_status(0); break;
    default: break;
    }
}

esp_err_t attribute_callback(esp_matter::attribute::callback_type_t, uint16_t, uint32_t, uint32_t,
    esp_matter_attr_val_t *, void *) { return ESP_OK; }

esp_err_t identify_callback(esp_matter::identification::callback_type_t, uint16_t endpoint, uint8_t,
    uint8_t, void *)
{
    ESP_LOGI(kTag, "Identify requested for pressure endpoint %u (serial indicator)", endpoint);
    return ESP_OK;
}
} // namespace

extern "C" void app_main()
{
    const esp_app_desc_t *description = esp_app_get_description();
    ESP_LOGI(kTag, "Firmware %s version %s; ESP-IDF %s; ESP32-C6, 4 MB, Matter over Thread + BLE",
        description->project_name, description->version, description->idf_ver);
    ESP_LOGI(kTag, "Wiring: sensor 3V3/GND, SDA=GPIO%d SCL=GPIO%d I2C=0x%02x; reset=GPIO%d",
        app_config::kSdaGpio, app_config::kSclGpio, app_config::kSensorAddress, app_config::kResetGpio);
    ESP_LOGI(kTag, "Scaling: MeasuredValue=lround(filtered Pa*10); HA kPa-labelled number intentionally means Pa");
    // No pressure attributes have the NONVOLATILE flag: samples never write NVS.
    // Leave corrupt NVS intact for diagnosis instead of silently erasing fabrics.
    ESP_ERROR_CHECK(nvs_flash_init());
    sensor_running = initialize_sensor(); // Missing sensor must not prevent Matter commissioning.

    esp_matter::node::config_t node_config;
    esp_matter::node_t *node = esp_matter::node::create(&node_config, attribute_callback, identify_callback);
    ESP_ERROR_CHECK(node ? ESP_OK : ESP_ERR_NO_MEM);
    esp_matter::endpoint::pressure_sensor::config_t config;
    config.identify.identify_type = chip::to_underlying(chip::app::Clusters::Identify::IdentifyTypeEnum::kNone);
    config.pressure_measurement.measured_value = nullptr; // Unknown until an actual successful reading.
    config.pressure_measurement.min_measured_value = -1250;
    config.pressure_measurement.max_measured_value = 1250;
    auto *endpoint = esp_matter::endpoint::pressure_sensor::create(node, &config, esp_matter::ENDPOINT_FLAG_NONE, nullptr);
    ESP_ERROR_CHECK(endpoint ? ESP_OK : ESP_ERR_NO_MEM);
    pressure_endpoint_id = esp_matter::endpoint::get_id(endpoint);
    ESP_LOGI(kTag, "Pressure sensor endpoint %u; Pressure Measurement cluster 0x0403", pressure_endpoint_id);

    esp_openthread_platform_config_t thread_config = {
        .radio_config = ESP_OPENTHREAD_DEFAULT_RADIO_CONFIG(),
        .host_config = ESP_OPENTHREAD_DEFAULT_HOST_CONFIG(),
        .port_config = ESP_OPENTHREAD_DEFAULT_PORT_CONFIG(),
    };
    set_openthread_platform_config(&thread_config);
    esp_err_t err = esp_matter::start(matter_event);
    ESP_LOGI(kTag, "Matter initialization: %s; native IEEE 802.15.4 Thread; Wi-Fi disabled", esp_err_to_name(err));
    ESP_ERROR_CHECK(err);
    ESP_ERROR_CHECK(chip::DeviceLayer::PlatformMgr().ScheduleWork(log_onboarding) == CHIP_NO_ERROR ? ESP_OK : ESP_FAIL);

    gpio_config_t button{};
    button.pin_bit_mask = 1ULL << app_config::kResetGpio;
    button.mode = GPIO_MODE_INPUT;
    button.pull_up_en = GPIO_PULLUP_ENABLE;
    ESP_ERROR_CHECK(gpio_config(&button));
    ESP_LOGI(kTag, "Factory reset: hold BOOT for %" PRIu32 " ms after boot, then release", app_config::kFactoryResetHoldMs);
    ESP_ERROR_CHECK(xTaskCreate(reset_button_task, "reset_button", 3072, nullptr, 2, nullptr) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(xTaskCreate(sensor_task, "sdp810", 4096, nullptr, 2, nullptr) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
}
