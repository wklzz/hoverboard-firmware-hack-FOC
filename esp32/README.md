# 🛰️ 平衡车 ESP32 智能中间控制器

> ESP32 通过 BLE 或 WiFi 接收指令和固件，经安全状态机过滤后通过 UART 转发给 STM32 Bootloader，实现无线 OTA 升级和远程控制。

---

## 一、系统架构

```
手机/电脑             RC 遥控接收机
  │ BLE/WiFi              │ PWM (CH1/CH2)
  ▼                       ▼
ESP32
  ├── AdapterFactory (BLE | WiFi | PWM)
  ├── HoverConnector  ── CRC 校验 & 帧解析
  ├── StateMachine    ── 安全状态过滤
  └── UART1 (TX=GPIO4, RX=GPIO5)
        │ 115200 baud
        ▼
STM32 Bootloader / Main Firmware
  └── OTA 模式 ↔ Runtime 模式
```

### 协议类型 A — STM32 Bootloader 协议 (OTA 模式)

此协议仅在进入 Bootloader 模式（主板开机前 1 秒或按下特定组合键）时有效，用于固件擦写。

| 偏移 | 字段 | 长度 | 说明 |
|------|------|------|------|
| 0 | SOF | 1 B | 固定 `0x7E` |
| 1 | CMD | 1 B | 指令 ID |
| 2-3 | LEN | 2 B LE | Payload 长度 |
| 4-N | Payload | N B | 业务数据 |
| N+1-N+2 | CRC16 | 2 B LE | CRC16-CCITT |

---

### 协议类型 B — 实时控制协议 (Runtime 模式)

一旦主固件正常启动，ESP32 必须切换到此协议以实现电机控制和数据监控。

#### 1. 控制指令 (ESP32 -> STM32)
- **频率**: 建议 10ms - 50ms 一次（超时 800ms 会报警）
- **格式**: 8 字节二进制包

| 偏移 | 字段 | 类型 | 说明 |
| :--- | :--- | :--- | :--- |
| 0-1 | **start** | `uint16_t` | 固定 `0xABCD` (小端: `CD AB`) |
| 2-3 | **steer** | `int16_t`  | 转向值 (-1000 ~ 1000) |
| 4-5 | **speed** | `int16_t`  | 速度值 (-1000 ~ 1000) |
| 6-7 | **checksum** | `uint16_t` | `start ^ steer ^ speed` |

#### 2. 数据回传 (STM32 -> ESP32)
- **格式**: 18 字节二进制包

| 偏移 | 字段 | 类型 | 说明 |
| :--- | :--- | :--- | :--- |
| 0-1 | **start** | `uint16_t` | 固定 `0xABCD` |
| 2-13 | **Data** | `int16_t[6]` | cmd1, cmd2, speedR, speedL, batV, temp |
| 14-15 | **led** | `uint16_t` | LED 状态 |
| 16-17 | **checksum** | `uint16_t` | 所有前续字段异或结果 |

### 协议类型 C — 手机/电脑通讯协议 (Unified Protocol)

为了简化客户端（小程序/App/PC）开发并增强安全性，手机/电脑与 ESP32 之间采用**统一帧格式**。该协议在结构上与“协议类型 A”完全对齐，支持在所有模式（OTA/Runtime）下使用，最大化代码复用。

#### 1. 统一帧结构

| 偏移 | 字段 | 长度 | 说明 |
|------|------|------|------|
| 0 | **SOF** | 1 B | 固定 `0x7E` |
| 1 | **CMD** | 1 B | 指令 ID (见下表) |
| 2-3 | **LEN** | 2 B LE | Payload 长度 (小端) |
| 4-N | **Payload** | N B | 业务数据 |
| N+1-N+2 | **CRC16** | 2 B LE | CRC16-CCITT (覆盖 CMD+LEN+Payload) |

#### 2. 指令定义 (CMD)

| CMD ID | 名称 | 功能说明 | Payload 格式 |
| :--- | :--- | :--- | :--- |
| **0x01-0x05** | **System** | 透传给 STM32 (PING, ERASE, WRITE, etc.) | 同协议 A |
| **0x10** | **DRIVE** | 实时电机控制指令 (路由至协议 B) | `steer(int16)`, `speed(int16)` |
| **0x20** | **CONFIG** | 设置 ESP32 本身参数 (如 WiFi SSID/密码) | `key(1B)`, `value(NB)` |
| **0x8x** | **ACK** | 指令成功响应 (CMD \| 0x80) | 见各组件定义 |
| **0x90** | **TELEMETRY**| 定时回传传感器数据 (由协议 B 封装) | 见下方说明 |

#### 3. Telemetry 数据格式 (CMD=0x90)

ESP32 将 STM32 回传的 18 字节原始包解析并封装在 0x90 指令的 Payload 中回传给手机。
- **Payload 长度**: 14 字节
- **内容**: `cmd1(int16), cmd2(int16), speedR(int16), speedL(int16), batV(int16), temp(int16), led(uint16)`

#### 4. 开发优势
- **代码复用**: ESP32 内部可直接复用 `protocol.h` 中的 `Packet` 结构体和 `crc16_ccitt()` 函数。
- **逻辑解耦**: 客户端只需维护一套 `0x7E` 协议栈，即可兼容升级与实时控制。
- **安全隔离**: 所有指令经由 ESP32 `StateMachine.h` 过滤，避免在运行中误触发 OTA 擦除。

---

### 安全状态机

```
IDLE ──PING──▶ CONNECTED
CONNECTED ──ERASE──▶ OTA
OTA ──BOOT──▶ IDLE
任意 ──故障──▶ ALARM（仅允许 PING）
RUNNING 状态下拒绝 ERASE / WRITE / BOOT
```

---

### PWM 控制适配器 (RC 遥控支持)

ESP32 具备硬件 PWM 信号捕获能力，可作为无线接收机与平衡车主板之间的智能桥梁。

*   **引脚分配**:
    *   **GPIO 6**: 捕获 CH1 (通常为方向控制)
    *   **GPIO 7**: 捕获 CH2 (通常为速度控制)
*   **信号处理**:
    *   标准 1000µs ~ 2000µs 脉宽自动映射至协议 B 的 `-1000 ~ 1000` 范围。
    *   内置 1500µs ±20µs 死区，防止零点漂移。
*   **安全保护**:
    *   **失控保护**: 若丢失信号超过 500ms，适配器自动向 STM32 发送零速度指令并报警。
    *   **模式隔离**: 只有在模式切换为 `1` (PWM 模式) 时，手动遥控才会生效，避免与手机指令冲突。
*   **持久化切换**:
    *   使用 `ble_debug.py` 的 `mode 1` 命令可在线切换。
    *   设置保存于 ESP32 NVS，断电重启后模式保持一致。

---

## 二、工程文件结构

```
esp32/
├── src/
│   ├── main.cpp         # 程序入口，NVS 模式持久化逻辑
│   ├── protocol.h       # 二进制协议 & CRC16
│   ├── IAdapter.h       # 适配器抽象接口
│   ├── AdapterFactory.h # 工厂类
│   ├── BLEAdapter.h     # BLE 适配器（Nordic UART Service）
│   ├── WiFiAdapter.h    # WiFi 适配器（HTTP OTA 端点）
│   ├── PwmAdapter.h     # PWM 捕获适配器（捕获 RC 信号）
│   ├── StateMachine.h   # 安全状态机
│   └── HoverConnector.h # 核心连接器（解析/转发/状态管理/上电静音）
├── ble_debug.py         # 强大的 Python BLE 调试 & OTA 脚本
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
| **GPIO 4** | UART1 TX | PB11 (USART3 RX) |
| **GPIO 5** | UART1 RX | PB10 (USART3 TX) |
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
# 打开串口监视器（原生 USB-CDC 常用 /dev/ttyACM0）
screen /dev/ttyACM0 115200
# 或使用 PlatformIO
pio device monitor -e hover_ble
```

正常启动日志：
```
========================================
  Hoverboard ESP32 Connector v1.0
========================================
[HC] UART to STM32 ready. TX=4 RX=5 Baud=115200
[MAIN] PWM Adapter initialized on pins 6/7 (Disabled).
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