# 平衡车 BLE 智能控制器通讯协议手册 (v1.0)

本手册详细介绍了手机（小程序/App）与 ESP32 中间控制器之间的蓝牙通讯协议。该协议（内部称为 **Protocol C**）旨在确保平衡车在远程控制、参数配置及固件升级（OTA）过程中的安全与实时性。

---

## 1. 物理层连接

*   **技术标准**：蓝牙 5.0 (BLE)
*   **服务名称**：`HoverBoard-OTA` (默认)
*   **服务 UUID** (Nordic UART Service 标准)：
    *   **Service**: `6E400001-B5A3-F393-E0A9-E50E24DCCA9E`
    *   **RX (手机写入)**: `6E400002-B5A3-F393-E0A9-E50E24DCCA9E` (Write / Write Without Response)
    *   **TX (芯片通知)**: `6E400003-B5A3-F393-E0A9-E50E24DCCA9E` (Notify)

---

## 2. 帧格式定义 (Protocol C)

所有的通讯数据包都遵循统一的二进制帧结构，采用**小端序 (Little Endian)**。

| 字节偏移 | 字段 | 长度 | 说明 |
| :--- | :--- | :--- | :--- |
| 0 | **SOF** | 1 字节 | 帧起始符，固定为 `0x7E` |
| 1 | **CMD** | 1 字节 | 指令 ID。响应包的 ID 为 `请求ID | 0x80` |
| 2 - 3 | **LEN** | 2 字节 | Payload (有效载荷) 的长度 |
| 4 - (N+3)| **Payload** | N 字节 | 具体指令携带的数据内容 |
| (N+4) - (N+5)| **CRC16** | 2 字节 | 校验和。算法：CRC16-CCITT (覆盖从 CMD 到 Payload) |

---

## 3. 指令集详解

### 3.1 身份认证 (Authentication)
为了安全起见，连接蓝牙后必须先通过认证，否则 ESP32 不会转发任何控制指令。

1.  **请求挑战 (AUTH_REQ, 0x30)**:
    *   手机发送：空 Payload。
    *   芯片回复：4 字节随机数 (Challenge)。
2.  **提交响应 (AUTH_RES, 0x31)**:
    *   手机发送：`Challenge ^ 0x12345678` (4 字节)。
    *   芯片回复：`0xB1` (成功) 或 丢弃连接 (失败)。

### 3.2 运动控制 (DRIVE, 0x10)
用于实时控制平衡车的移动。

*   **Payload (4 字节)**:
    *   `steer`: `int16` (转向，左负右正，范围 -1000 ~ 1000)
    *   `speed`: `int16` (速度，前正后负，范围 -1000 ~ 1000)
*   **频率建议**：20Hz - 50Hz。

### 3.3 实时数据回传 (TELEMETRY, 0x90)
芯片会定期将 STM32 的传感器数据打包推送给手机。

*   **Payload (14 字节)**:
    *   `cmd1/cmd2`: 内部控制量。
    *   `speedR/L`: 左右轮实时转速。
    *   `batVoltage`: 电池电压 (单位: 0.01V)。
    *   `boardTemp`: 主板温度 (单位: 0.1°C)。

### 3.4 ESP32 固件升级 (OTA)
用于无线更新 ESP32 自身的固件。

1.  **OTA_BEGIN (0x40)**: 发送固件总大小 (uint32) 及目标芯片类型。
    *   Payload: `[size:uint32][type:uint8]` (type 为 0 代表 ESP32)。
2.  **OTA_DATA (0x41)**: 分片传输数据。
    *   Payload: `[offset:uint32][data:N]`。
3.  **OTA_END (0x42)**: 传输完成，触发重启。

### 3.5 STM32 固件升级 (透传模式)
用于无线更新平衡车主板 (STM32) 的固件。ESP32 在此模式下仅作为蓝牙转串口的“桥梁”。

1.  **进入升级模式 (Handover)**:
    当手机发送 `PING/INFO/ERASE` 等指令时，ESP32 会自动向 STM32 发送 `$REBOOT\r\n` 字符串，强制其重启进入 Bootloader 状态。
2.  **透传指令 (Protocol A)**:
    以下指令会被 ESP32 直接转发给 STM32，手机需按照以下格式封装：
    *   **PING (0x01)**: 握手，确认 Bootloader 在线。
    *   **INFO (0x02)**: 获取 STM32 的 Flash 分区和版本信息。
    *   **ERASE (0x03)**: 擦除 STM32 目标 App 区域。
    *   **WRITE (0x04)**: 写入固件数据块。
    *   **BOOT (0x05)**: 指令 STM32 退出 Bootloader 并启动新程序。

---

## 4. 安全保护机制

1.  **指令看门狗 (Watchdog)**:
    如果 ESP32 在 **500ms** 内没有收到有效的 `DRIVE` 指令，会立即给电机发送停止指令，防止手机掉线或 App 卡死导致车辆失控。
2.  **连接独占**:
    一旦有手机连入并认证成功，ESP32 会立即停止蓝牙广播，其他手机无法发现或连接，防止他人恶意干扰。
3.  **状态隔离**:
    在 OTA 升级过程中，状态机会强制锁死 `DRIVE` 指令，确保升级时电机绝不会意外转动。

---

## 5. 开发者建议

*   **MTU 设置**：本协议支持并将 MTU 协商为 **517 字节**。建议在连接后优先请求 MTU 交换，以提高 OTA 传输效率。
*   **校验计算**：
    ```cpp
    // CRC16-CCITT 示例 (C++)
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)(data[i] << 8);
        for (int j = 0; j < 8; j++) {
            crc = (crc & 0x8000) ? (crc << 1) ^ 0x1021 : crc << 1;
        }
    }
    ```
