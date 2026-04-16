#!/bin/bash
# ============================================================
# ESP32 平衡车中间控制器 — Docker 编译与烧录脚本
# 用法:
#   ./build.sh              — 编译 BLE 模式 (默认)
#   ./build.sh wifi         — 编译 WiFi 模式
#   ./build.sh flash        — 编译 BLE 模式并烧录 (需连接 ESP32)
#   ./build.sh flash wifi   — 编译 WiFi 模式并烧录
#   ./build.sh clean        — 清理编译缓存
# ============================================================

set -e

IMAGE_NAME="esp32-hoverboard-builder"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# 解析参数
MODE="ble"
FLASH=false
for arg in "$@"; do
    case "$arg" in
        wifi)   MODE="wifi" ;;
        flash)  FLASH=true ;;
        clean)
            echo "==== 清理编译缓存 ===="
            rm -rf "$SCRIPT_DIR/.pio"
            echo "已清理 .pio 目录"
            exit 0
            ;;
    esac
done

ENV_NAME="hover_${MODE}"
echo "========================================================"
echo "  ESP32 Hoverboard Connector — 构建脚本"
echo "  模式: ${MODE^^}   环境: ${ENV_NAME}"
echo "========================================================"

# --- 步骤 1: 构建 Docker 镜像 ---
echo ""
echo "==== [1/3] 构建 Docker 构建镜像 ===="
docker build -t "$IMAGE_NAME" "$SCRIPT_DIR"

if [ $? -ne 0 ]; then
    echo "==== 镜像构建失败！ ===="
    exit 1
fi

echo "==== 镜像构建成功 ===="

# --- 步骤 2: 编译固件 ---
echo ""
echo "==== [2/3] 编译固件 (env: ${ENV_NAME}) ===="
docker run --rm \
    -v "$SCRIPT_DIR:/workspace" \
    "$IMAGE_NAME" \
    run -e "$ENV_NAME"

if [ $? -ne 0 ]; then
    echo "==== 编译失败！ ===="
    exit 1
fi

BIN_PATH="$SCRIPT_DIR/.pio/build/${ENV_NAME}/firmware.bin"
echo ""
echo "==== 编译成功！ ===="
echo "固件路径: ${BIN_PATH}"
ls -lh "$BIN_PATH" 2>/dev/null || true

# --- 步骤 3: 烧录 (可选) ---
if [ "$FLASH" = true ]; then
    echo ""
    echo "==== [3/3] 烧录固件到 ESP32 ===="

    # 自动检测串口
    PORT=""
    for p in /dev/ttyUSB0 /dev/ttyUSB1 /dev/ttyACM0 /dev/ttyACM1; do
        if [ -e "$p" ]; then
            PORT="$p"
            break
        fi
    done

    if [ -z "$PORT" ]; then
        echo "==== 未找到串口设备，请手动指定 PORT 环境变量 ===="
        echo "例如: PORT=/dev/ttyUSB0 ./build.sh flash"
        exit 1
    fi

    echo "使用串口: $PORT"

    # 主机直接烧录 (需要安装 esptool)
    if command -v esptool.py &>/dev/null; then
        esptool.py --chip esp32 --port "$PORT" --baud 921600 \
            write_flash -z 0x1000 "$BIN_PATH"
    else
        # 回退到 pio 烧录
        docker run --rm \
            --device="$PORT:$PORT" \
            -v "$SCRIPT_DIR:/workspace" \
            "$IMAGE_NAME" \
            run -e "$ENV_NAME" --target upload \
            --upload-port "$PORT"
    fi

    if [ $? -eq 0 ]; then
        echo "==== 烧录成功！ ===="
        echo ""
        echo "提示: 使用以下命令查看串口输出:"
        echo "  screen $PORT 115200"
        echo "  或: minicom -D $PORT -b 115200"
    else
        echo "==== 烧录失败！请检查连接和权限 ===="
        exit 1
    fi
fi

echo ""
echo "==== 全部完成 ===="
