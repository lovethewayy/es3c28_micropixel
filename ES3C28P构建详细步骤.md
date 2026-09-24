# 在 ES3C28P 上构建并烧录 MicroPixel 固件 —— 详细步骤

> 目标：用 `micropixel-es3c28p-src-full.zip`（已含全部子模块源码，解压即构建）在你自己电脑上编译出
> `build/host-esp32s3-es3c28p/micropixel-full.bin` 并烧录到 ES3C28P。
> 支持 **macOS 或 Linux**（wamrc 仅支持这两个宿主系统）。全程在仓库根目录执行。

---

## 第 0 步：准备硬件与主机环境

- 一台 macOS 或 Linux 电脑，能联网。
- ES3C28P 开发板 + **可传数据的 Type-C 线**（充电线不能用）。
- 已安装：`git`、`cmake`、`python3`（3.10+）。检查：
  ```sh
  git --version && cmake --version && python3 --version
  ```
- 磁盘预留约 **15 GB**（ESP-IDF 工具链 + 编译产物）。

---

## 第 1 步：安装 ESP-IDF 6.1

MicroPixel 固件锁定 ESP-IDF 6.1，必须用 6.1 分支。

```sh
# 1) 克隆 ESP-IDF（放到你喜欢的位置，例如 ~/esp/esp-idf）
mkdir -p ~/esp && cd ~/esp
git clone --recursive -b v6.1 https://github.com/espressif/esp-idf.git
cd esp-idf

# 2) 安装工具链（只需要 esp32s3 目标，避免装全平台）
./install.sh esp32s3
```

安装完成后，**每个新终端**都需要激活它：
```sh
source ~/esp/esp-idf/export.sh
```

---

## 第 2 步：安装 WASI SDK 33

编译 Guest（WebAssembly）应用需要 WASI SDK。从 GitHub 下载 **WASI SDK 33** 的
`wasi-sdk-33-...-linux.tar.gz`（Linux）或 `...-macos.tar.gz`（macOS）：

- 下载页：https://github.com/WebAssembly/wasi-sdk/releases

解压并记住路径（例：`~/wasi-sdk-33`）。后面在 `.env` 里写 `WASI_SDK_PATH`。

---

## 第 3 步：解压源码工程

把 `micropixel-es3c28p-src-full.zip` 拷到电脑上解压：
```sh
unzip micropixel-es3c28p-src-full.zip -d ~/micropixel
cd ~/micropixel
```
> 本包已包含 WAMR、esp-iot-solution 两个子模块源码，**无需**再执行 `git submodule`。

---

## 第 4 步：配置根目录 `.env`

在仓库根目录新建文件 `.env`（与 `tools/s3.sh` 读取的一致）：
```sh
# 必填：激活后的 ESP-IDF 路径
IDF_PATH=~/esp/esp-idf

# 必填：WASI SDK 33 的路径
WASI_SDK_PATH=~/wasi-sdk-33

# 可选：烧录串口与波特率（不填则自动识别）
# ES3C28P_S3_PORT=/dev/cu.usbmodemXXX
# ES3C28P_S3_BAUD=921600
```

> `.env` 只是便于配置；也可以在每次终端里 export 等效变量。

---

## 第 5 步：安装 Python 依赖

```sh
source $IDF_PATH/export.sh          # 激活 IDF 与它的 Python 环境
python3 -m pip install -r requirements-dev.txt
```
`requirements-dev.txt` 内容为 `pyserial>=3.5,<4` 与 `Pillow>=10.1,<13`（烧录与截图用）。

---

## 第 6 步：编译 Xtensa 后端 wamrc（一次性）

MicroPixel 用定制的 WAMR 生成 Xtensa AOT v6 应用，需要一个特殊构建的 `wamrc`。
只需做一次，产物在 `build/tools/wamrc-xtensa/wamrc`：

```sh
bash tools/build_wamrc_xtensa.sh
```
首次会先编译 Xtensa 版 LLVM（耗时较长），最后打印 `wamrc --version` 即成功。

---

## 第 7 步：构建 Host + Apps + 全量镜像（核心）

确认当前终端已 `source $IDF_PATH/export.sh`，然后：

```sh
# 一条命令：构建 Host（es3c28p 板型）+ 7 个示例 App 的 Xtensa AOT 包 + 全量可烧录镜像
bash tools/s3.sh build-release es3c28p
```

**产物**：`build/host-esp32s3-es3c28p/micropixel-full.bin`（浏览器在线烧录页用）。
分步等价命令：
```sh
bash tools/s3.sh build-host es3c28p     # 只构建 Host 固件
bash tools/s3.sh build-apps             # 构建 7 个示例 App 的 AOT Bundle 与 app_store
```

> 若想重新完整干净构建：`bash tools/s3.sh fullclean-host`（可选，一般不需要）。

---

## 第 8 步：烧录

用数据线连接 ES3C28P。三个方式任选：

```sh
# 方式 A：一步烧 Host + App Store（推荐首次）
bash tools/s3.sh flash-all es3c28p <串口>

# 方式 B：分步
bash tools/s3.sh flash-host es3c28p <串口>   # 只烧 Host，保留已装 App
bash tools/s3.sh flash-apps es3c28p <串口>   # 烧 App Store

# 方式 C：浏览器在线烧录（用 build-release 的 micropixel-full.bin）
# 打开 https://micropixel.ai/flash（或仓库 README 指向的在线烧录页）选择该 bin
```
串口可以不填，工具会按 ESP32-S3 芯片特征自动识别；多设备时才需指定。

---

## 第 9 步：查看启动日志

```sh
bash tools/s3.sh monitor es3c28p <串口> --reset
```
出现类似 `es3c28p_esp32s3: ready: ILI9341 + FT6336, native Wi-Fi, audio=ES8311` 表示启动成功。

---

## 第 10 步：首次上板需实测的硬件项（重要）

首次点亮后，按 `docs/development/es3c28p-bring-up.zh-CN.md` 第 3 节核对 5 项：
1. **颜色顺序**：若红蓝对调，改 `display_hardware.cpp` 为 `LCD_RGB_ELEMENT_ORDER_BGR` 后重编。
2. **是否反相**：若像照片负片，在 swap_xy 前加 `esp_lcd_panel_invert_color(state.panel, true)`。
3. **I2S_DO/DI**：当前按 DO=IO6（Arduino/MicroPython 文档）；若无声，把 `data_out` 对调验证。
4. **编解码器 I2C**：地址 0x18，与触摸共用 IO15/IO16；音频配置失败先查这里。
5. **PSRAM/Flash 变体**：默认 N16R8（octal）；若是 Quad 变体，在
   `firmware/espressif/sdkconfig.s3-es3c28p.defaults` 覆盖 `CONFIG_SPIRAM_MODE_OCT`。

---

## 常见问题

| 现象 | 处理 |
|---|---|
| `idf.py` 找不到 | 确认已 `source $IDF_PATH/export.sh`（每个新终端都要） |
| `wamrc: No such file` | 先跑 `bash tools/build_wamrc_xtensa.sh`（步骤 6） |
| 烧录报端口不存在 | 换数据线；`ls /dev/cu.usbmodem*`（mac）/`ls /dev/ttyUSB*`（Linux）看是否识别 |
| 编译报 WASI 相关错误 | 确认 `WASI_SDK_PATH` 指向 WASI SDK 33 |
| 屏幕花屏/颜色错 | 看步骤 10 的第 1、2 项 |
| 没有声音 | 看步骤 10 的第 3、4 项 |

---

## 产物与位置速查

| 物 | 位置 |
|---|---|
| 全量可烧录镜像 | `build/host-esp32s3-es3c28p/micropixel-full.bin` |
| Host 固件 | `build/host-esp32s3-es3c28p/micropixel.bin` |
| Xtensa wamrc | `build/tools/wamrc-xtensa/wamrc` |
| 板级源码 | `firmware/espressif/main/platform/boards/es3c28p-esp32s3/` |
| 适配指南 | `docs/development/es3c28p-bring-up.zh-CN.md` |
