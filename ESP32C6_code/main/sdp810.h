#pragma once

#include <driver/i2c_master.h>
#include "pressure_math.h"

class Sdp810 {
public:
    esp_err_t start(pressure::Reading &first_reading);
    esp_err_t read(pressure::Reading &reading);
private:
    esp_err_t init_bus();
    esp_err_t command(uint16_t command);
    i2c_master_bus_handle_t bus_ = nullptr;
    i2c_master_dev_handle_t device_ = nullptr;
};
