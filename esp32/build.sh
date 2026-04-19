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

# --- 步骤 0: 更新代码 ---
echo "==== [0/3] 更新代码 (Git Pull) ===="
if [ -d "$SCRIPT_DIR/../.git" ]; then
    git -C "$SCRIPT_DIR/.." pull || echo "警告: Git pull 失败，将使用本地代码继续。"
else
    echo "未检测到 Git 仓库，跳过更新。"
fi

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
    -v "pio_cache:/root/.platformio" \
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

# --- 步骤 2.5: 展示分区表 ---
echo ""
echo "==== [2.5/3] 展示分区表 (确认 OTA 布局) ===="
docker run --rm \
    --entrypoint /bin/sh \
    -v "pio_cache:/root/.platformio" \
    -v "$SCRIPT_DIR:/workspace" \
    "$IMAGE_NAME" \
    -c 'python3 $(find /root/.platformio/packages -name gen_esp32part.py | head -n 1) /workspace/partitions.csv /tmp/partitions.bin'

# --- 步骤 3: 烧录 (可选) ---
if [ "$FLASH" = true ]; then
    echo ""
    echo "==== [3/3] 烧录固件到 ESP32 ===="

    # 自动检测串口 (优先检测 ACM0，通常是原生 USB 接口；其次 USB0)
    if [ -z "$PORT" ]; then
        for p in /dev/ttyACM0 /dev/ttyACM1 /dev/ttyUSB0 /dev/ttyUSB1; do
            if [ -e "$p" ]; then
                PORT="$p"
                break
            fi
        done
    fi
    
    if [ -z "$PORT" ]; then
        echo "==== 未找到串口设备，请手动指定 PORT 环境变量 ===="
        echo "例如: PORT=/dev/ttyUSB0 ./build.sh flash"
        exit 1
    fi

    echo "使用串口: $PORT"

    # 检查当前用户是否有权访问该端口
    if [ ! -w "$PORT" ]; then
        echo "==== 错误: 当前用户对于 $PORT 没有写入权限！ ===="
        echo "这通常是因为该设备属于 'dialout' 组，但您不在该组中。"
        echo "解决方法 (二选一):"
        echo "  1. 运行: sudo usermod -a -G dialout $USER  (然后重启设备生效)"
        echo "  2. 运行: sudo chmod 666 $PORT"
        exit 1
    fi

    # 主机直接烧录 (需要安装 esptool)
    if command -v esptool.py &>/dev/null; then
        esptool.py --chip esp32 --port "$PORT" --baud 921600 \
            write_flash -z 0x1000 "$BIN_PATH"
    else
        # 回退到 pio 烧录
        docker run --rm \
            -v "pio_cache:/root/.platformio" \
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
