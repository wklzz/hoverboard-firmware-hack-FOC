#pragma once
#include <Arduino.h>
#include <HardwareSerial.h>
#include <vector>
#include "IAdapter.h"
#include "StateMachine.h"
#include "protocol.h"

// ============================================================
// HoverConnector — 核心协调层
//
// 职责：
//  1. 接收来自适配器（BLE/WiFi）的原始字节
//  2. 解析二进制数据包
//  3. 经状态机安全预检
//  4. 将合法指令通过硬件串口转发给 STM32 Bootloader
//  5. 将 STM32 的响应回传给原始适配器
// ============================================================

class HoverConnector {
public:
    // uart: 连接 STM32 的硬件串口 (如 Serial2)
    // txPin/rxPin: 对应 GPIO 引脚
    HoverConnector(HardwareSerial& uart, int txPin, int rxPin, uint32_t baud = 115200)
        : _uart(uart), _txPin(txPin), _rxPin(rxPin), _baud(baud) {}

    // 注册适配器（支持多个，互相独立）
    void addAdapter(IAdapter* adapter) {
        adapter->onRawData = [this](const uint8_t* data, size_t len) {
            _rxBuffer.insert(_rxBuffer.end(), data, data + len);
            processBuffer();
        };
        _adapters.push_back(adapter);
        adapter->init();
    }

    // 必须在 setup() 中调用
    void begin() {
        _uart.begin(_baud, SERIAL_8N1, _rxPin, _txPin);
        Serial.printf("[HC] UART to STM32 ready. TX=%d RX=%d Baud=%lu\n",
                      _txPin, _rxPin, _baud);
    }

    // 必须在 loop() 中调用 — 转发 STM32 回包给适配器
    void update() {
        _stm32RxBuffer.clear();
        // 以非阻塞方式读取 STM32 回包 (最多等 200ms)
        uint32_t start = millis();
        while (millis() - start < 200) {
            if (_uart.available()) {
                uint8_t b = _uart.read();
                _stm32RxBuffer.push_back(b);
                // 帧结束判断: 最简方式是在收到 SOF 后的一帧
                if (_stm32RxBuffer.size() > 2) {
                    uint16_t expected = 6 + ((uint16_t)_stm32RxBuffer[2] | ((uint16_t)_stm32RxBuffer[3] << 8));
                    if (_stm32RxBuffer.size() >= expected) break;
                }
            }
        }

        if (!_stm32RxBuffer.empty()) {
            broadcastToAdapters(_stm32RxBuffer.data(), _stm32RxBuffer.size());
            // 状态机后处理
            if (_stm32RxBuffer.size() >= 2) {
                _sm.onCommandExecuted(_stm32RxBuffer[1]);
            }
        }
    }

    StateMachine& stateMachine() { return _sm; }

private:
    // 尝试从接收缓冲区解析出一个完整帧并转发
    void processBuffer() {
        // 找 SOF
        while (!_rxBuffer.empty() && _rxBuffer[0] != PKT_SOF) {
            _rxBuffer.erase(_rxBuffer.begin());
        }
        if (_rxBuffer.size() < 6) return; // 帧最短 6 字节

        uint16_t payloadLen = (uint16_t)_rxBuffer[2] | ((uint16_t)_rxBuffer[3] << 8);
        size_t frameLen = 6 + payloadLen;

        if (payloadLen > MAX_PAYLOAD_LEN) {
            // 数据异常，丢弃一个字节重找帧头
            _rxBuffer.erase(_rxBuffer.begin());
            return;
        }
        if (_rxBuffer.size() < frameLen) return; // 等更多数据

        // 提取帧
        uint8_t frame[6 + MAX_PAYLOAD_LEN];
        memcpy(frame, _rxBuffer.data(), frameLen);
        _rxBuffer.erase(_rxBuffer.begin(), _rxBuffer.begin() + frameLen);

        // CRC 校验 (覆盖 CMD + LEN + Payload)
        uint16_t calc = crc16_ccitt(&frame[1], 3 + payloadLen);
        uint16_t recv = (uint16_t)frame[4 + payloadLen] | ((uint16_t)frame[5 + payloadLen] << 8);
        if (calc != recv) {
            Serial.printf("[HC] CRC error: calc=0x%04X recv=0x%04X\n", calc, recv);
            return;
        }

        uint8_t cmdId = frame[1];

        // 状态机预检
        if (!_sm.canExecute(cmdId)) return;

        // 转发至 STM32
        Serial.printf("[HC] -> STM32 CMD=0x%02X len=%u\n", cmdId, payloadLen);
        _uart.write(frame, frameLen);
        _uart.flush();
    }

    void broadcastToAdapters(const uint8_t* data, size_t len) {
        for (auto* adapter : _adapters) {
            adapter->send(data, len);
        }
    }

    HardwareSerial&         _uart;
    int                     _txPin, _rxPin;
    uint32_t                _baud;
    StateMachine            _sm;
    std::vector<IAdapter*>  _adapters;
    std::vector<uint8_t>    _rxBuffer;
    std::vector<uint8_t>    _stm32RxBuffer;
};
