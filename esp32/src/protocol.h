#pragma once
#include <cstdint>
#include <cstring>

// ============================================================
// 统一二进制通讯协议定义
//
// ── 协议类型 A / C (手机 ↔ ESP32, ESP32 ↔ STM32 OTA)
//    帧格式: [SOF:1][CMD:1][LEN:2 LE][Payload:N][CRC16:2 LE]
//    SOF = 0x7E, CRC16-CCITT (初始值 0xFFFF, 覆盖 CMD+LEN+Payload)
//
// ── 协议类型 B (ESP32 ↔ STM32 主固件 实时控制)
//    控制帧:   [0xCD 0xAB][steer:int16][speed:int16][xor:uint16]  (8 B)
//    回传帧:   [0xCD 0xAB][data:int16*6][led:uint16][xor:uint16] (18 B)
// ============================================================

// ── 帧定界符 ──────────────────────────────────────────────
static constexpr uint8_t  PKT_SOF         = 0x7E;
static constexpr uint8_t  ACK_MASK        = 0x80;
static constexpr uint16_t MAX_PAYLOAD_LEN = 256;

// ── 协议类型 A 指令 (OTA 透传, 0x01–0x05) ─────────────────
enum class CmdId : uint8_t {
    PING   = 0x01,   // 心跳 / 握手
    INFO   = 0x02,   // 获取 Flash 布局
    ERASE  = 0x03,   // 擦除 App 区
    WRITE  = 0x04,   // 写入 Flash 数据块
    BOOT   = 0x05,   // 跳转到 App

    // ── 协议类型 C 扩展指令 (手机/PC 专属) ──────────────────
    DRIVE      = 0x10,  // 实时电机控制: [steer:int16][speed:int16]
    STATUS     = 0x11,  // 查询 ESP32 状态 (Payload 为空)
    CONFIG     = 0x20,  // 设置 ESP32 本地参数: [key:uint8][value:N bytes]
    AUTH_REQ   = 0x30,  // 身份认证挑战 (ESP32->Client): [challenge:uint32]
    AUTH_RES   = 0x31,  // 身份认证响应 (Client->ESP32): [response:uint32]
    TELEMETRY  = 0x90,  // 传感器数据回传 (ESP32→手机): 封装 Protocol B 数据
};

// ── 协议类型 A/C 帧结构 ────────────────────────────────────
struct Packet {
    uint8_t  sof;                       // 0x7E
    uint8_t  cmd;                       // CmdId 或 (CmdId | ACK_MASK)
    uint16_t len;                       // Payload 长度 (小端)
    uint8_t  payload[MAX_PAYLOAD_LEN];
    uint16_t crc;                       // CRC16-CCITT (小端)
};

// ── 协议类型 B 帧结构 (STM32 主固件通讯) ─────────────────
static constexpr uint16_t RUNTIME_SOF = 0xABCD;

struct RuntimeCmd {                     // ESP32 → STM32 (8 B)
    uint16_t start   = RUNTIME_SOF;
    int16_t  steer   = 0;
    int16_t  speed   = 0;
    uint16_t checksum;

    void calcChecksum() {
        checksum = start ^ (uint16_t)steer ^ (uint16_t)speed;
    }
};

struct RuntimeFeedback {                // STM32 → ESP32 (18 B)
    uint16_t start;
    int16_t  cmd1;
    int16_t  cmd2;
    int16_t  speedR_meas;
    int16_t  speedL_meas;
    int16_t  batVoltage;
    int16_t  boardTemp;
    uint16_t cmdLed;
    uint16_t checksum;

    bool isValid() const {
        uint16_t xor_ = start ^ (uint16_t)cmd1 ^ (uint16_t)cmd2
                      ^ (uint16_t)speedR_meas ^ (uint16_t)speedL_meas
                      ^ (uint16_t)batVoltage ^ (uint16_t)boardTemp ^ cmdLed;
        return start == RUNTIME_SOF && xor_ == checksum;
    }
};

// ── CRC16-CCITT (与 STM32 代码完全一致) ────────────────────
inline uint16_t crc16_ccitt(const uint8_t* data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)(data[i] << 8);
        for (int j = 0; j < 8; j++) {
            crc = (crc & 0x8000) ? (crc << 1) ^ 0x1021 : crc << 1;
        }
    }
    return crc;
}

// ── 构造原始字节帧 (A/C 协议通用) ──────────────────────────
// 返回帧长度, 失败返回 0
inline size_t build_frame(uint8_t cmd, const uint8_t* payload, uint16_t plen,
                           uint8_t* out, size_t out_max) {
    if (out_max < (size_t)(6 + plen)) return 0;

    out[0] = PKT_SOF;
    out[1] = cmd;
    out[2] = (uint8_t)(plen & 0xFF);
    out[3] = (uint8_t)(plen >> 8);
    if (plen > 0 && payload) memcpy(&out[4], payload, plen);

    // CRC 覆盖 CMD + LEN + Payload
    uint16_t crc = crc16_ccitt(&out[1], 3 + plen);
    out[4 + plen] = (uint8_t)(crc & 0xFF);
    out[5 + plen] = (uint8_t)(crc >> 8);
    return 6 + plen;
}
