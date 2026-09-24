#include "platform/boards/es3c28p-esp32s3/i2s_audio_sink.hpp"

#include "esp_codec_dev_defaults.h"
#include "platform/buses/i2c_executor.hpp"

namespace micropixel::platform::es3c28p_esp32s3 {

// ES3C28P audio: ES8311 codec on the shared I2C bus (address 0x18), I2S data
// MCK=IO4 / SCK=IO5 / DO=IO6 / LRC=IO7 / DI=IO8, and the FM8002E amplifier
// enable on IO1 (active low). DO/DI follows the LCDWIKI Arduino and
// MicroPython reference docs; the ESP-IDF demo sheet lists them swapped, so
// verify audio output and flip .data_out/.data_in if silent.
I2sAudioSink::I2sAudioSink()
    : sink_(
          {
              .name = "ES3C28P ES8311/I2S",
              .log_tag = "es3c28p_audio",
              .i2c_port = I2C_NUM_0,
              .i2s_port = I2S_NUM_0,
              .master_clock = GPIO_NUM_4,
              .bit_clock = GPIO_NUM_5,
              .word_select = GPIO_NUM_7,
              .data_out = GPIO_NUM_6,
              .amplifier_enable = GPIO_NUM_1,
              // esp_codec_dev publishes the legacy 8-bit wire address; the IDF
              // master-bus API takes the corresponding 7-bit device address.
              .codec_i2c_address = static_cast<uint8_t>(ES8311_CODEC_DEFAULT_ADDR >> 1U),
              .sample_rate = 16000U,
              .i2c_clock_hz = 100000U,
              .amplifier_preroll_ms = 24U,
              .dma_descriptor_count = 6U,
              .dma_frame_count = 240U,
              .amplifier_active_low = true,
              .probe_before_attach = true,
          },
          {.amplifier_voltage = 3.3F, .codec_dac_voltage = 3.3F}) {}

esp_err_t I2sAudioSink::Configure(i2c_master_bus_handle_t bus, buses::I2cExecutor& executor) {
    return sink_.Configure(bus, executor);
}

}  // namespace micropixel::platform::es3c28p_esp32s3
