FROM python:3.11-slim

LABEL description="ESP32 PlatformIO Build Environment for Hoverboard Connector"

# 安装系统依赖
RUN apt-get update && apt-get install -y \
    curl \
    git \
    udev \
    && rm -rf /var/lib/apt/lists/*

# 安装 PlatformIO Core
RUN pip install --no-cache-dir platformio

# 预热 ESP32 平台和工具链（避免首次构建时联网）
# 通过创建虚拟项目来触发平台安装
RUN mkdir -p /tmp/init_env/src && \
    echo '[env:esp32dev]\nplatform=espressif32\nboard=esp32dev\nframework=arduino' \
    > /tmp/init_env/platformio.ini && \
    echo 'void setup(){} void loop(){}' > /tmp/init_env/src/main.cpp && \
    cd /tmp/init_env && pio run && \
    rm -rf /tmp/init_env

WORKDIR /workspace

# 容器入口：运行 pio 命令
ENTRYPOINT ["pio"]
CMD ["run"]
