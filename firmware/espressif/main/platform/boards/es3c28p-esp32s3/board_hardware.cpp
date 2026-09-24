#include "platform/boards/es3c28p-esp32s3/board_hardware.hpp"

#include <algorithm>

#include "driver/ledc.h"
#include "esp_check.h"

namespace micropixel::platform::es3c28p_esp32s3 {
namespace {

constexpr char kTag[] = "es3c28p_s3_hw";
constexpr i2c_port_num_t kI2cPort = I2C_NUM_0;
constexpr gpio_num_t kI2cSda = GPIO_NUM_16;
constexpr gpio_num_t kI2cScl = GPIO_NUM_15;
constexpr gpio_num_t kBacklight = GPIO_NUM_45;
constexpr ledc_channel_t kBacklightChannel = LEDC_CHANNEL_0;
constexpr ledc_timer_t kBacklightTimer = LEDC_TIMER_1;
constexpr uint32_t kBacklightMaximumDuty = (1U << 10U) - 1U;

}  // namespace

esp_err_t BoardHardware::Initialize() {
    if (i2c_bus_ != nullptr) {
        return ESP_ERR_INVALID_STATE;
    }
    // Shared I2C bus: FT6336G touch (IO16/IO15) and the ES8311 codec both
    // sit on this single bus on the ES3C28P.
    i2c_master_bus_config_t bus_config{};
    bus_config.i2c_port = kI2cPort;
    bus_config.sda_io_num = kI2cSda;
    bus_config.scl_io_num = kI2cScl;
    bus_config.clk_source = I2C_CLK_SRC_DEFAULT;
    bus_config.glitch_ignore_cnt = 7U;
    bus_config.flags.enable_internal_pullup = true;
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_config, &i2c_bus_), kTag, "create shared I2C bus failed");

    // TFT_BL (IO45): active high, LEDC PWM drives the backlight.
    ledc_timer_config_t timer_config{};
    timer_config.speed_mode = LEDC_LOW_SPEED_MODE;
    timer_config.duty_resolution = LEDC_TIMER_10_BIT;
    timer_config.timer_num = kBacklightTimer;
    timer_config.freq_hz = 5000U;
    timer_config.clk_cfg = LEDC_AUTO_CLK;
    ESP_RETURN_ON_ERROR(ledc_timer_config(&timer_config), kTag, "configure backlight timer failed");
    ledc_channel_config_t channel_config{};
    channel_config.gpio_num = kBacklight;
    channel_config.speed_mode = LEDC_LOW_SPEED_MODE;
    channel_config.channel = kBacklightChannel;
    channel_config.timer_sel = kBacklightTimer;
    channel_config.duty = 0U;
    channel_config.hpoint = 0;
    channel_config.flags.output_invert = false;
    ESP_RETURN_ON_ERROR(ledc_channel_config(&channel_config), kTag, "configure backlight channel failed");
    brightness_initialized_ = true;
    return ESP_OK;
}

esp_err_t BoardHardware::SetBrightness(int percent) {
    if (!brightness_initialized_) {
        return ESP_ERR_INVALID_STATE;
    }
    const uint32_t bounded = static_cast<uint32_t>(std::clamp(percent, 0, 100));
    const uint32_t duty = (kBacklightMaximumDuty * bounded + 50U) / 100U;
    ESP_RETURN_ON_ERROR(ledc_set_duty(LEDC_LOW_SPEED_MODE, kBacklightChannel, duty), kTag, "set backlight duty failed");
    return ledc_update_duty(LEDC_LOW_SPEED_MODE, kBacklightChannel);
}

}  // namespace micropixel::platform::es3c28p_esp32s3
