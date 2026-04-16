FROM python:3.9-slim

# 安装 PlatformIO 核心和必要的工具
RUN pip install -U platformio && \
    apt-get update && \
    apt-get install -y gcc-arm-none-eabi binutils-arm-none-eabi && \
    apt-get clean

# 设置工作目录
WORKDIR /workspace

# 预装 STM32 平台包以提速（可选）
RUN pio platform install ststm32