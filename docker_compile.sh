#!/bin/bash

IMAGE_NAME="hoverboard-foc-pio-builder"
ENV_NAME=${1:-"VARIANT_PWM_BL"}

# 1. 检查镜像
if [[ "$(docker images -q $IMAGE_NAME 2> /dev/null)" == "" ]]; then
    echo "==== 构建镜像中... ===="
    docker build -t $IMAGE_NAME .
fi

echo "==== 正在编译环境: $ENV_NAME ===="

# 2. 核心修正：
# 我们把 pio_core 放在宿主机的当前目录下，但通过环境变量映射
# 这样即便容器销毁，工具链也不用重新下载，且权限始终属于你
mkdir -p .pio_core

docker run --rm \
    --user $(id -u):$(id -g) \
    -v "$(pwd):/workspace" \
    -v "$(pwd)/.pio_core:/tmp/pio_core" \
    -e PLATFORMIO_CORE_DIR=/tmp/pio_core \
    -e HOME=/tmp \
    -w /workspace \
    $IMAGE_NAME pio run -e $ENV_NAME

# 3. 自动验证逻辑保持不变
if [ $? -eq 0 ]; then
    echo "==== 编译成功！ ===="
    echo "==== 自动验证地址偏移... ===="
    arm-none-eabi-nm -n .pio/build/$ENV_NAME/firmware.elf | head -n 5 | grep "08004000" && echo "✅ 地址验证通过: 0x08004000" || echo "❌ 警告: 地址偏移可能不正确！"
else
    echo "==== 编译失败，请检查上方报错 ===="
    exit 1
fi