#pragma once
#include <cstdint>
#include <cstring>

// ============================================================
// 二进制通讯协议定义 (与 STM32 Bootloader 完全对齐)
// 帧格式: [SOF:1][CMD:1][LEN:2 LE][Payload:N][CRC16:2 LE]
// ============================================================

static constexpr uint8_t  PKT_SOF         = 0x7E;
static constexpr uint8_t  ACK_MASK        = 0x80;
static constexpr uint16_t MAX_PAYLOAD_LEN = 256;

// 指令 ID
enum class CmdId : uint8_t {
    PING  = 0x01,   // 心跳 / 握手
    INFO  = 0x02,   // 获取 Flash 布局
    ERASE = 0x03,   // 擦除 App 区
    WRITE = 0x04,   // 写入 Flash 数据块
    BOOT  = 0x05,   // 跳转到 App
};

// 帧结构
struct Packet {
    uint8_t  sof;           // 0x7E
    uint8_t  cmd;           // CmdId 或 (CmdId | ACK_MASK)
    uint16_t len;           // Payload 长度 (小端)
    uint8_t  payload[MAX_PAYLOAD_LEN];
    uint16_t crc;           // CRC16-CCITT (小端)
};

// CRC16-CCITT 计算 (与 STM32 代码完全一致)
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

// 构造原始字节帧
// 返回帧长度
inline size_t build_frame(uint8_t cmd, const uint8_t* payload, uint16_t plen,
                           uint8_t* out, size_t out_max) {
    if (out_max < (size_t)(5 + plen)) return 0;

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
