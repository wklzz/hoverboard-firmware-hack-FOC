# 🛰️ 平衡车 ESP32 智能中间控制器

> ESP32 通过 BLE 或 WiFi 接收指令和固件，经安全状态机过滤后通过 UART 转发给 STM32 Bootloader，实现无线 OTA 升级和远程控制。

---

## 一、系统架构

```
手机/电脑
  │ BLE (Nordic UART) 或 HTTP POST /ota
  ▼
ESP32
  ├── AdapterFactory (BLEAdapter | WiFiAdapter)
  ├── HoverConnector  ── CRC 校验 & 帧解析
  ├── StateMachine    ── 安全状态过滤
  └── UART2 (TX=GPIO17, RX=GPIO16)
        │ 115200 baud
        ▼
STM32 Bootloader (PB10/PB11)
  └── CMD_ERASE → CMD_WRITE → CMD_BOOT → 主固件
```

### 通讯协议（与 STM32 Bootloader 完全对齐）

| 偏移 | 字段 | 长度 | 说明 |
|------|------|------|------|
| 0 | SOF | 1 B | 固定 `0x7E` |
| 1 | CMD | 1 B | 指令 ID（见下表）|
| 2-3 | LEN | 2 B LE | Payload 长度 |
| 4-N | Payload | N B | 业务数据 |
| N+1-N+2 | CRC16 | 2 B LE | CRC16-CCITT（覆盖 CMD+LEN+Payload）|

| CMD | 说明 |
|-----|------|
| `0x01` PING | 握手心跳 |
| `0x02` INFO | 获取 Flash 布局 |
| `0x03` ERASE | 擦除 App 区 |
| `0x04` WRITE | 写入 Flash 块 |
| `0x05` BOOT | 跳转到 App |

### 安全状态机

```
IDLE ──PING──▶ CONNECTED
CONNECTED ──ERASE──▶ OTA
OTA ──BOOT──▶ IDLE
任意 ──故障──▶ ALARM（仅允许 PING）
RUNNING 状态下拒绝 ERASE / WRITE / BOOT
```

---

## 二、工程文件结构

```
esp32/
├── src/
│   ├── main.cpp         # 程序入口，模式选择
│   ├── protocol.h       # 二进制协议 & CRC16
│   ├── IAdapter.h       # 适配器抽象接口
│   ├── AdapterFactory.h # 工厂类
│   ├── BLEAdapter.h     # BLE 适配器（Nordic UART Service）
│   ├── WiFiAdapter.h    # WiFi 适配器（HTTP OTA 端点）
│   ├── StateMachine.h   # 安全状态机
│   └── HoverConnector.h # 核心连接器（解析/转发/状态管理）
├── platformio.ini       # PlatformIO 双环境配置
├── Dockerfile           # 预装工具链的编译镜像
├── build.sh             # 一键编译 & 烧录脚本
└── README.md            # 本文档
```

---

## 三、硬件接线

> ⚠️ **注意：ESP32 是 3.3V 逻辑，STM32 USART3 也是 3.3V，可直连。绝对不要接 15V 电源线！**

| ESP32 GPIO | 功能 | STM32 引脚 |
|------------|------|-----------|
| **GPIO17** | UART2 TX | PB11 (USART3 RX) |
| **GPIO16** | UART2 RX | PB10 (USART3 TX) |
| **GND** | 公共地 | GND |

---

## 四、快速开始

### 4.1 修改配置

编辑 `src/main.cpp` 顶部的配置区：

```cpp
#define ADAPTER_MODE    0         // 0 = BLE, 1 = WiFi
#define BLE_DEVICE_NAME "HoverBoard-OTA"
#define WIFI_SSID       "YourSSID"
#define WIFI_PASSWORD   "YourPassword"
```

---

### 4.2 编译固件

#### 方式 A — Docker 一键构建（推荐，无需本地环境）

```bash
cd esp32/

# 编译 BLE 模式（默认）
./build.sh

# 编译 WiFi 模式
./build.sh wifi

# 编译并自动烧录（ESP32 需已连接 USB）
./build.sh flash
./build.sh flash wifi

# 清理缓存
./build.sh clean
```

#### 方式 B — 本地 PlatformIO

```bash
# 安装 PlatformIO
pip install platformio

# 编译
pio run -e hover_ble      # BLE 模式
pio run -e hover_wifi     # WiFi 模式

# 编译并烧录
pio run -e hover_ble -t upload
```

---

### 4.3 烧录后验证

```bash
# 打开串口监视器（波特率 115200）
screen /dev/ttyUSB0 115200
# 或使用 PlatformIO
pio device monitor -e hover_ble
```

正常启动日志：
```
========================================
  Hoverboard ESP32 Connector v1.0
========================================
[HC] UART to STM32 ready. TX=17 RX=16 Baud=115200
[BLE] Advertising started: HoverBoard-OTA
[MAIN] Setup complete. Waiting for connection...
```

---

### 4.4 执行 OTA 升级

**BLE 模式（手机/小程序）：**
1. 扫描并连接 `HoverBoard-OTA` 设备
2. 按协议格式发送数据包：`PING` → `ERASE` → 多个 `WRITE` → `BOOT`
3. 观察 ESP32 串口日志确认每个阶段状态

**WiFi 模式（局域网 HTTP）：**
```bash
# 获取 ESP32 IP（查看串口日志）
# 上传固件 .bin 文件
curl -X POST http://<ESP32_IP>/ota \
     -H "Content-Type: application/octet-stream" \
     --data-binary @build/firmware.bin
```

也可以直接复用 `bootloader/test.py`，但需要修改 `SERIAL_PORT` 为 TCP Socket（`socket://ESP32_IP:23`）。

---

## 五、扩展指南

### 添加新通讯方式（如 4G/MQTT）
1. 创建 `src/MQTTAdapter.h`，继承 `IAdapter`，实现 `init()` / `send()` 方法
2. 在 `AdapterFactory.h` 中添加 `AdapterType::MQTT` 分支
3. 在 `main.cpp` 中选择即可，**其他代码无需修改**

### 引入电机控制指令
在 `StateMachine.h` 中添加 `RUNNING` 状态转换逻辑，并在 `HoverConnector.h` 的 `processBuffer()` 中增加电机控制路由即可与 OTA 流程完全隔离。