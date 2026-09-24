# ES3C28P 适配指南

ES3C28P（ES3N28P 为无触摸版本）是 LCDWIKI 的 2.8 寸 IPS ESP32-S3 显示模块：
240x320、ILI9341V 驱动、FT6336G 电容触摸、ES8311 音频编解码器与 FM8002E 功放。
主控为 ESP32-S3（Xtensa LX7 双核，N16R8：16 MiB Flash + 8 MiB octal PSRAM）。

本板复用 `esp32-s3-common` 的 Landscape320（320x240 横屏）Host UI profile、LVGL 显示
与 PSRAM displayed shadow。板级层只负责引脚映射、共享 I²C 总线（触摸 + 编解码器）、
ILI9341 SPI 面板、LEDC 背光与 ES8311/I2S 音频输出。

## 1. 已接入能力

- ILI9341 显示：SPI 40 MHz，320x240 横屏（240x320 竖屏面板经 swap_xy 旋转）。
- FT6336G 触摸：复用 `esp_lcd_touch_ft5x06` 驱动与 `ReadFt6336Report`。
- ES8311 音频输出 + FM8002E 功放（IO1 低有效）。
- 原生 ESP32-S3 Wi-Fi、USB Serial/JTAG 本地控制与 JPEG 截图。
- BOOT 键（IO0）映射为确认键；IO2 作为唯一扩展 GPIO。
- 与所有 S3 板型一致的 Xtensa Guest AOT 基线、Guest Graphics 与 App Hall。

SD 卡、电池 ADC、RGB LED 与麦克风未开放给 Guest Service，不在本板 profile 内。

## 2. 引脚映射（来自 LCDWIKI ESP-IDF 参考文档）

| 外设 | 信号 | GPIO | 说明 |
|---|---|---|---|
| LCD | TFT_CS | IO10 | 片选，低有效 |
| | TFT_RS(DC) | IO46 | 高=数据，低=命令 |
| | TFT_SCK | IO12 | SPI 时钟 |
| | TFT_MOSI | IO11 | SPI 写数据 |
| | TFT_MISO | IO13 | SPI 读数据（未使用） |
| | TFT_RST | CHIP_PU | 与主控复位共用，无独立复位脚 |
| | TFT_BL | IO45 | 背光，高=亮 |
| 触摸 | TP_SDA | IO16 | I2C 数据 |
| | TP_SCL | IO15 | I2C 时钟 |
| | TP_RST | IO18 | 复位，低有效 |
| | TP_INT | IO17 | 中断，触摸时拉低 |
| 音频 | Audio_EN | IO1 | 功放使能，低=使能 |
| | I2S_MCK | IO4 | 主时钟 |
| | I2S_SCK | IO5 | 位时钟 |
| | I2S_DO | IO6 | 数据输出 |
| | I2S_LRC | IO7 | 左右声道 |
| | I2S_DI | IO8 | 数据输入（未使用） |
| SD | SD_CLK/CMD/D0-D3 | IO38/40/39/41/48/47 | 未开放 |
| 电池 | BAT_ADC | IO9 | 未开放 |
| LED | RGB_INT | IO42 | 未开放 |
| 按键 | BOOT_KEY | IO0 | 确认键 |
| USB | USB_N/USB_P | IO19/IO20 | 原生 USB Serial/JTAG |

## 3. 首次上板需实测的硬件项

代码默认值尽量贴近多数 ES3C28P 模块，但以下几项无法脱离真机确定，务必按
[测量与验收](#5-测量与验收) 验证：

1. **颜色顺序**：默认 `LCD_RGB_ELEMENT_ORDER_RGB`。若颜色红蓝对调，改为
   `LCD_RGB_ELEMENT_ORDER_BGR`（见 `display_hardware.cpp`）。
2. **颜色反相**：ILI9341 一般无需反相，默认未调用 `esp_lcd_panel_invert_color`。
   若画面呈照片负片效果，在 `display_hardware.cpp` 的 swap_xy 前加一行
   `esp_lcd_panel_invert_color(state.panel, true)`。
3. **I2S_DO / I2S_DI**：LCDWIKI Arduino 与 MicroPython 文档一致标注
   DO=IO6 / DI=IO8，ESP-IDF 文档写反。当前按 DO=IO6。若无声，把
   `i2s_audio_sink.cpp` 中 `data_out` 与 `data_in`（DI 未接，无需）对调验证。
4. **编解码器 I2C 总线**：板上仅有一路 I2C（IO15/IO16），ES8311（地址 0x18）
   与触摸共用该总线。若音频配置失败，先确认编解码器确实接在此总线上。
5. **PSRAM / Flash 变体**：默认按 N16R8（octal PSRAM，16 MiB Flash）构建，与
   `sdkconfig.s3.defaults` 一致。若你手上的模块是 N8R2 / N16R2（Quad PSRAM），
   在 `sdkconfig.s3-es3c28p.defaults` 中覆盖 `CONFIG_SPIRAM_MODE_OCT`。

## 4. 构建与烧录

前置：仓库根目录配置好 ESP-IDF（见 [烧录指南](flashing.zh-CN.md)），并在根 `.env`
中设置 `IDF_PATH`（以及可选的 `ES3C28P_S3_PORT`、`ES3C28P_S3_BAUD`）。

```sh
# 1) 构建本板 Host（含 Xtensa Guest Runtime）
bash tools/s3.sh build-host es3c28p

# 2) 构建 7 个示例 App 的 Xtensa AOT Bundle 与共享 app_store（所有 S3 板共用）
bash tools/s3.sh build-apps

# 3) 一键：构建 Host + Apps，并生成浏览器可烧录的 micropixel-full.bin
bash tools/s3.sh build-release es3c28p

# 4) 烧录 Host（保留 App Store）后烧录 App Store；或一步到位：
bash tools/s3.sh flash-host es3c28p <port>
bash tools/s3.sh flash-apps es3c28p <port>
#   等价于：
bash tools/s3.sh flash-all es3c28p <port>

# 5) 监视启动日志（可选 --reset 复位捕捉启动）
bash tools/s3.sh monitor es3c28p <port> --reset
```

也可用兼容别名 `build-es3c28p` / `flash-es3c28p` / `monitor-es3c28p` /
`port-es3c28p`。端口通过 `tools/firmware.py` 按 ESP32-S3 芯片特征自动识别。

Guest 应用（`guest/apps/*`）是架构无关的 WebAssembly 源码，不需要按板型改动；
`--aot-target xtensa` 已由 `s3.sh build-apps` / `build-release` 统一使用。
开发单个 App 时：

```sh
export PATH="$PWD:$PATH"
export WASI_SDK_PATH=/path/to/wasi-sdk
export WAMRC=/absolute/path/to/micropixel-wamrc-xtensa
micropixel --transport usb run guest/apps/sdk-demo
```

## 5. 测量与验收

沿用 [ESP32-S3 适配指南](esp32-s3-box-3-bring-up.zh-CN.md) 的四层测量思路，至少
连续运行 10 分钟，要求无画面损坏、爆音、watchdog 或持续内存下降：

- 面板传输：整屏、不同高度 strip、多脏区、单/双缓冲下的 bytes 与撕裂。
- CPU 合成：fill、opaque copy、alpha、scale、text。
- Guest Scene：Snake 小 damage、Blocks、粒子、滚屏。
- 并发：音频、Wi-Fi、解码、截图、暂停恢复的 P95/P99 与触控延迟。

颜色验证覆盖纯色、灰阶、裁剪、alpha 边界与缩放；UI 验证覆盖触摸边缘、长标题、
键盘与 Guest/Hall 切换；AOT 验证覆盖大模块、trap、watchdog 与反复冷启动。

## 6. 板级源码位置

- `firmware/espressif/main/platform/boards/es3c28p-esp32s3/` — 板级实现。
- `firmware/espressif/sdkconfig.s3-es3c28p.defaults` — 板级 sdkconfig 默认值。
- `firmware/espressif/main/platform/boards/esp32-s3-common/` — 共享 Landscape320 显示/UI。
- `tools/s3.sh`、`tools/firmware_profiles.json` — 板型注册。
- `firmware/espressif/main/Kconfig.projbuild` — `MICROPIXEL_BOARD_ES3C28P` 选项。

## 7. 硬件资料来源

- LCDWIKI 产品页：https://www.lcdwiki.com/2.8inch_ESP32-S3_Display
- ESP-IDF 参考示例引脚表：https://www.lcdwiki.com/res/ES3C28P/2.8inch_ES3C28P_ES3N28P_ESP-IDF_Demo_Instructions.pdf
- 用户手册（ES8311 地址 0x18）：https://www.lcdwiki.com/res/ES3C35P/3.5inch_IPS_ESP32-S3_User_Manual.pdf
