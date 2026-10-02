#include "sdp810.h"
#include "app_config.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

esp_err_t Sdp810::init_bus()
{
    if (!bus_) {
        i2c_master_bus_config_t config{};
        config.i2c_port = I2C_NUM_0;
        config.sda_io_num = static_cast<gpio_num_t>(app_config::kSdaGpio);
        config.scl_io_num = static_cast<gpio_num_t>(app_config::kSclGpio);
        config.clk_source = I2C_CLK_SRC_DEFAULT;
        config.glitch_ignore_cnt = 7;
        // External 4.7 kOhm pullups to 3V3 are required for dependable wiring.
        config.flags.enable_internal_pullup = true;
        esp_err_t err = i2c_new_master_bus(&config, &bus_);
        if (err != ESP_OK) return err;
    }
    if (!device_) {
        i2c_device_config_t config{};
        config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
        config.device_address = app_config::kSensorAddress;
        config.scl_speed_hz = app_config::kI2cClockHz;
        return i2c_master_bus_add_device(bus_, &config, &device_);
    }
    return ESP_OK;
}

esp_err_t Sdp810::command(uint16_t value)
{
    const uint8_t bytes[] = {static_cast<uint8_t>(value >> 8), static_cast<uint8_t>(value)};
    return i2c_master_transmit(device_, bytes, sizeof(bytes), app_config::kI2cTimeoutMs);
}

esp_err_t Sdp810::start(pressure::Reading &first_reading)
{
    esp_err_t err = init_bus();
    if (err != ESP_OK) return err;
    vTaskDelay(pdMS_TO_TICKS(30)); // Datasheet power-up >=25 ms; also wakes a sleeping sensor.
    err = i2c_master_probe(bus_, app_config::kSensorAddress, app_config::kI2cTimeoutMs);
    if (err != ESP_OK) return err;
    // A previous ESP reset may leave the separately powered sensor measuring.
    // Stop first. Idle sensors may NACK this; the following start/read is authoritative.
    (void)command(0x3FF9);
    vTaskDelay(pdMS_TO_TICKS(2)); // Stop-to-next-command minimum 500 us.
    err = command(0x3615); // Continuous DIFFERENTIAL PRESSURE, average until read.
    if (err != ESP_OK) return err;
    vTaskDelay(pdMS_TO_TICKS(30)); // First result >=8 ms; allow initial 12 ms settling.
    return read(first_reading);
}

esp_err_t Sdp810::read(pressure::Reading &reading)
{
    if (!device_) return ESP_ERR_INVALID_STATE;
    uint8_t frame[9];
    esp_err_t err = i2c_master_receive(device_, frame, sizeof(frame), app_config::kI2cTimeoutMs);
    if (err != ESP_OK) return err;
    return pressure::decode(frame, reading) ? ESP_OK : ESP_ERR_INVALID_CRC;
}
