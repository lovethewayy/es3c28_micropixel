#!/usr/bin/env bash
# ============================================================
#  MicroPixel ES3C28P 一键构建脚本 (macOS / Linux)
#  用法: 把本脚本和 micropixel-es3c28p-src-full.zip 放同一目录,
#        先解压 zip, 再运行:
#            bash 一键构建.sh
#  结果: build/host-esp32s3-es3c28p/micropixel-full.bin
# ============================================================
set -euo pipefail

# ---- 可改的配置 ----
ESP_IDF_BRANCH="v6.1"
ESP_IDF_DIR="${ESP_IDF_DIR:-$HOME/esp/esp-idf}"
WASI_SDK_TAG="wasi-sdk-33"
WASI_SDK_VERSION="wasi-sdk-33.0"
WASI_SDK_DIR="${WASI_SDK_DIR:-$HOME/wasi-sdk-33.0}"

# 自动定位仓库根目录（本脚本所在目录 = 仓库根目录）
WORK="$(cd "$(dirname "$0")" && pwd)"
cd "$WORK"
echo "==> 仓库目录: $WORK"

# ---- [1/6] 基础工具 ----
echo "==> [1/6] 检查基础工具"
for c in git cmake python3 unzip curl; do
    if ! command -v "$c" >/dev/null 2>&1; then
        echo "缺少命令: $c  请先安装 (如: brew install $c / apt install $c)" >&2
        exit 1
    fi
done
echo "    基础工具 OK (git cmake python3 unzip curl)"

# ---- [2/6] 安装 ESP-IDF 6.1 (esp32s3) ----
echo "==> [2/6] 安装/定位 ESP-IDF $ESP_IDF_BRANCH (esp32s3) 到 $ESP_IDF_DIR"
if [ ! -d "$ESP_IDF_DIR" ]; then
    mkdir -p "$(dirname "$ESP_IDF_DIR")"
    git clone --recursive -b "$ESP_IDF_BRANCH" https://github.com/espressif/esp-idf.git "$ESP_IDF_DIR"
fi
if [ ! -x "$ESP_IDF_DIR/install.sh" ]; then
    echo "ESP-IDF 目录无效: $ESP_IDF_DIR  (若你已装 ESP-IDF, 请设 ESP_IDF_DIR=路径 重跑)" >&2
    exit 1
fi
if [ ! -d "$ESP_IDF_DIR/tools/idf_tools.py" ]; then :; fi
"$ESP_IDF_DIR/install.sh" esp32s3
# 激活（本脚本内有效）
set +e; source "$ESP_IDF_DIR/export.sh"; set -e
echo "    ESP-IDF 就绪"

# ---- [3/6] 安装 WASI SDK 33 ----
echo "==> [3/6] 安装 WASI SDK $WASI_SDK_VERSION 到 $WASI_SDK_DIR"
if [ ! -d "$WASI_SDK_DIR" ]; then
    case "$(uname -s)" in
        Darwin) plat="macos";;
        Linux)  plat="linux";;
        *) echo "仅支持 macOS/Linux" >&2; exit 2;;
    esac
    case "$(uname -m)" in
        x86_64|amd64) arch="x86_64";;
        arm64|aarch64) arch="arm64";;
        *) echo "不支持的 CPU 架构: $(uname -m)" >&2; exit 2;;
    esac
    url="https://github.com/WebAssembly/wasi-sdk/releases/download/${WASI_SDK_TAG}/${WASI_SDK_VERSION}-${arch}-${plat}.tar.gz"
    echo "    下载 $url"
    curl -fL "$url" -o /tmp/wasi-sdk.tar.gz
    mkdir -p "$WASI_SDK_DIR"
    tar -xzf /tmp/wasi-sdk.tar.gz -C "$WASI_SDK_DIR" --strip-components=1
    rm -f /tmp/wasi-sdk.tar.gz
fi
echo "    WASI SDK 就绪"

# ---- [4/6] 写 .env ----
echo "==> [4/6] 写入 .env"
cat > "$WORK/.env" <<EOF
IDF_PATH=$ESP_IDF_DIR
WASI_SDK_PATH=$WASI_SDK_DIR
EOF
echo "    .env 已写入"

# ---- [5/6] Python 依赖 + Xtensa wamrc ----
echo "==> [5/6] 安装 Python 依赖并编译 Xtensa wamrc（首次较慢）"
python3 -m pip install -r "$WORK/requirements-dev.txt"
bash "$WORK/tools/build_wamrc_xtensa.sh"
echo "    wamrc 就绪"

# ---- [6/6] 构建 Host + Apps + 全量镜像 ----
echo "==> [6/6] 构建 Host + Apps + 全量镜像（最耗时步骤）"
bash "$WORK/tools/s3.sh build-release es3c28p"

echo ""
echo "======================================================"
echo " 完成! 可烧录文件:"
echo "   $WORK/build/host-esp32s3-es3c28p/micropixel-full.bin"
echo " 烧录(接好 Type-C 数据线):"
echo "   bash tools/s3.sh flash-all es3c28p"
echo "======================================================"
