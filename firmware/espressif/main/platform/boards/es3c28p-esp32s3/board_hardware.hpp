#pragma once

#include <cstdint>

#include "driver/i2c_master.h"
#include "esp_err.h"

namespace micropixel::platform::es3c28p_esp32s3 {

// Board-level wiring for the LCDWIKI ES3C28P (ESP32-S3, 2.8" ILI9341V).
//
// The LCD chip select (IO10) is a direct GPIO owned by the SPI panel IO, and
// the FM8002E amplifier enable (IO1, active low) is driven directly by the
// audio sink, so this class only owns the shared I2C bus (touch + codec) and
// the LEDC backlight channel (IO45, active high).
class BoardHardware final {
   public:
    [[nodiscard]] esp_err_t Initialize();
    [[nodiscard]] esp_err_t SetBrightness(int percent);
    [[nodiscard]] i2c_master_bus_handle_t I2cBus() const { return i2c_bus_; }

   private:
    i2c_master_bus_handle_t i2c_bus_{};
    bool brightness_initialized_{};
};

}  // namespace micropixel::platform::es3c28p_esp32s3
