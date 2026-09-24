#pragma once

#include <array>

#include "device/contracts/peripheral_channel.hpp"
#include "driver/gpio.h"

namespace micropixel::platform::es3c28p_esp32s3::board {

struct ExpansionGpioLine final {
    device::PeripheralChannelId channel;
    const char* name;
};

// LCDWIKI ES3C28P (2.8" IPS, ESP32-S3, N16R8) pin allocation.
//   LCD (ILI9341V, 240x320, SPI): CS=IO10, DC=IO46, SCK=IO12, MOSI=IO11,
//     MISO=IO13, RST=CHIP_PU (shared), BL=IO45 (high = on)
//   Touch (FT6336G, I2C): SDA=IO16, SCL=IO15, RST=IO18, INT=IO17
//   Audio (ES8311 + FM8002E): EN=IO1 (low = enable), I2S MCK=IO4, SCK=IO5,
//     DO=IO6, LRC=IO7, DI=IO8
//   SD card (SDIO 1-bit): CLK=IO38, CMD=IO40, D0=IO39, D1=IO41, D2=IO48, D3=IO47
//   Battery ADC=IO9, RGB LED=IO42, USB=IO19/IO20, UART0=IO43/IO44, BOOT=IO0
//
// IO2 is broken out on the board's expansion header and is not used by any
// onboard peripheral, so it is exposed to Guest apps as a single GPIO line.
// Audit additional free pins against the board schematic before enabling more.
inline constexpr std::array<device::PeripheralChannelId, 1U> kApplicationGpioLines{2U};
inline constexpr gpio_num_t kBootButton = GPIO_NUM_0;
inline constexpr std::array<ExpansionGpioLine, 1U> kExpansionGpioLines{{
    {2U, "Expansion header IO2"},
}};

}  // namespace micropixel::platform::es3c28p_esp32s3::board
