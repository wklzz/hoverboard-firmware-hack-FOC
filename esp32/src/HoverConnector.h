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
//  1. 接收来自适配器的原始字节，解析 Protocol C 帧
//  2. 处理 ESP32 本地指令 (CONFIG, STATUS)
//  3. 将控制指令 (DRIVE) 转换为 Protocol B 后发送给 STM32
//  4. 将透传指令 (OTA 等) 直接发送给 STM32
//  5. 接收 STM32 串口数据，区分 OTA 回包与 Protocol B 回包
//  6. 封装为 Protocol C 回传给适配器
// ============================================================

class HoverConnector {
public:
    HoverConnector(HardwareSerial& uart, int txPin, int rxPin, uint32_t baud = 115200)
        : _uart(uart), _txPin(txPin), _rxPin(rxPin), _baud(baud) {}

    void addAdapter(IAdapter* adapter) {
        adapter->onRawData = [this](const uint8_t* data, size_t len) {
            _rxBuffer.insert(_rxBuffer.end(), data, data + len);
            processBuffer();
        };
        adapter->onConnectionLost = [this]() {
            handleDisconnection();
        };
        _adapters.push_back(adapter);
        adapter->init();
    }

    void begin() {
        _uart.begin(_baud, SERIAL_8N1, _rxPin, _txPin);
        Serial.printf("[HC] UART to STM32 ready. TX=%d RX=%d Baud=%lu\n",
                      _txPin, _rxPin, _baud);
    }

    void update() {
        // 1. 读取 STM32 返回的串口数据
        while (_uart.available()) {
            uint8_t b = _uart.read();
            _stm32RxBuffer.push_back(b);
        }

        if (!_stm32RxBuffer.empty()) {
            // 判断是 Protocol A 的回包 还是 Protocol B 的 Telemetry 包
            if (_stm32RxBuffer[0] == PKT_SOF) {
                processSTM32ProtocolA();
            } else {
                processSTM32ProtocolB();
            }
        }

        // 2. 定期向 STM32 发送心跳包 (Protocol B)
        // 只要不是在 OTA 或 ALARM 状态，就以 50ms 频率维持指令，防止 STM32 报警
        if (_sm.current != SystemState::OTA && _sm.current != SystemState::ALARM) {
            if (millis() - _lastHeartbeat > 50) {
                _lastHeartbeat = millis();
                _currentDrive.calcChecksum();
                _uart.write((uint8_t*)&_currentDrive, sizeof(RuntimeCmd));
            }
        }
    }

    StateMachine& stateMachine() { return _sm; }
    std::function<void(uint8_t key, const uint8_t* payload, uint16_t len)> onConfig;
    std::function<uint16_t(uint8_t* payload, uint16_t maxLen)> onStatusRequest;

private:
    // --------------------------------------------------------
    // 从手机侧接收数据并处理
    // --------------------------------------------------------
    void processBuffer() {
        // 使用 while 循环排空缓冲区，处理多帧堆积
        while (_rxBuffer.size() >= 6) {
            // 1. 查找 SOF (0x7E)
            if (_rxBuffer[0] != PKT_SOF) {
                _rxBuffer.erase(_rxBuffer.begin());
                continue;
            }

            // 2. 解析长度 (小端)
            uint16_t payloadLen = (uint16_t)_rxBuffer[2] | ((uint16_t)_rxBuffer[3] << 8);
            if (payloadLen > MAX_PAYLOAD_LEN) {
                // 非法长度，丢弃该 SOF 寻找下一个
                _rxBuffer.erase(_rxBuffer.begin());
                continue;
            }

            size_t frameLen = 6 + payloadLen;
            if (_rxBuffer.size() < frameLen) {
                // 帧数据未收全，保留在缓冲区等待后续字节
                break;
            }

            // 3. 提取完整帧并立即从缓冲区移除，防止解析失败导致死循环
            uint8_t frame[6 + MAX_PAYLOAD_LEN];
            memcpy(frame, _rxBuffer.data(), frameLen);
            _rxBuffer.erase(_rxBuffer.begin(), _rxBuffer.begin() + frameLen);

            // 4. 校验 CRC16-CCITT
            uint16_t calc = crc16_ccitt(&frame[1], 3 + payloadLen);
            uint16_t recv = (uint16_t)frame[4 + payloadLen] | ((uint16_t)frame[5 + payloadLen] << 8);
            if (calc != recv) {
                Serial.printf("[HC] CRC error: calc=0x%04X recv=0x%04X\n", calc, recv);
                continue;
            }

            // 5. 状态机安全性检查
            uint8_t cmdId = frame[1];
            if (!_sm.canExecute(cmdId)) {
                Serial.printf("[HC] StateMachine BLOCKED CMD: 0x%02X\n", cmdId);
                continue;
            }

            // 6. 根据 Protocol C 进行指令路由
            CmdId id = static_cast<CmdId>(cmdId & ~0x80);
            switch (id) {
                case CmdId::STATUS:
                    handleStatusCmd();
                    break;
                case CmdId::CONFIG:
                    handleConfigCmd(&frame[4], payloadLen);
                    break;
                case CmdId::DRIVE:
                    handleDriveCmd(&frame[4], payloadLen);
                    break;
                case CmdId::PING:
                case CmdId::INFO:
                case CmdId::ERASE:
                case CmdId::WRITE:
                case CmdId::BOOT:
                    // OTA 相关指令处理
                    if (_sm.current != SystemState::OTA) {
                        startOtaHandover();
                        _sm.current = SystemState::OTA; 
                    }
                    Serial.printf("[HC] -> STM32 OTA CMD=0x%02X len=%u\n", cmdId, payloadLen);
                    _uart.write(frame, frameLen);
                    _uart.flush();
                    break;
                default:
                    Serial.printf("[HC] -> STM32 Unknown CMD=0x%02X len=%u\n", cmdId, payloadLen);
                    _uart.write(frame, frameLen);
                    _uart.flush();
                    break;
            }
            _sm.onCommandExecuted(cmdId);
        }
    }

    void handleStatusCmd() {
        Serial.println("[HC] Handled STATUS cmd");
        uint8_t payload[8];
        payload[0] = static_cast<uint8_t>(_sm.current);
        uint16_t plen = 1;
        if (onStatusRequest) {
            plen += onStatusRequest(&payload[1], sizeof(payload) - 1);
        }
        sendResponseToAdapters(static_cast<uint8_t>(CmdId::STATUS) | ACK_MASK, payload, plen);
    }

    void handleConfigCmd(const uint8_t* payload, uint16_t len) {
        if (len >= 1 && onConfig) {
            onConfig(payload[0], &payload[1], len - 1);
        }
        sendResponseToAdapters(static_cast<uint8_t>(CmdId::CONFIG) | ACK_MASK, nullptr, 0);
    }

    void handleDriveCmd(const uint8_t* payload, uint16_t len) {
        if (len < 4) return;
        // 小端读取 steer, speed 并存入缓存，由 update() 循环发送
        _currentDrive.steer = (int16_t)(payload[0] | (payload[1] << 8));
        _currentDrive.speed = (int16_t)(payload[2] | (payload[3] << 8));
    }

    void startOtaHandover() {
        Serial.println("[HC] Handover: Sending $REBOOT to STM32...");
        _uart.write((const uint8_t*)"$REBOOT\r\n", 9);
        _uart.flush();
        // 等待 STM32 完成重启并进入 Bootloader
        delay(200);
    }

    void handleDisconnection() {
        Serial.println("[HC] Connection lost!");
        if (_sm.current == SystemState::RUNNING) {
            Serial.println("[HC] SAFETY STOP: Sending zero drive to STM32");
            RuntimeCmd stopCmd;
            stopCmd.steer = 0;
            stopCmd.speed = 0;
            stopCmd.calcChecksum();
            _uart.write((uint8_t*)&stopCmd, sizeof(RuntimeCmd));
        }
        _sm.reset();
    }

    // --------------------------------------------------------
    // 处理从 STM32 发来的数据
    // --------------------------------------------------------
    void processSTM32ProtocolA() {
        if (_stm32RxBuffer.size() < 6) return;
        
        uint16_t payloadLen = (uint16_t)_stm32RxBuffer[2] | ((uint16_t)_stm32RxBuffer[3] << 8);
        uint16_t expected = 6 + payloadLen;

        // 安全限制：如果长度不合理，或者超过我们的缓冲区余量，判定为噪声并丢弃 SOF
        if (payloadLen > MAX_PAYLOAD_LEN || expected > 300) {
            Serial.printf("[HC] Invalid Protocol A length from STM32: %u. Dropping byte.\n", payloadLen);
            _stm32RxBuffer.erase(_stm32RxBuffer.begin());
            return;
        }

        if (_stm32RxBuffer.size() >= expected) {
            broadcastToAdapters(_stm32RxBuffer.data(), expected);
            _sm.onCommandExecuted(_stm32RxBuffer[1]);
            _stm32RxBuffer.erase(_stm32RxBuffer.begin(), _stm32RxBuffer.begin() + expected);
        }
    }

    void processSTM32ProtocolB() {
        // 清理缓存开头不是 0xCD 0xAB (即开始为 0xABCD, 第一个字节为 0xCD, 第二个字节为 0xAB) 的部分
        while (_stm32RxBuffer.size() >= 2) {
            if (_stm32RxBuffer[0] == 0xCD && _stm32RxBuffer[1] == 0xAB) break;
            // 处理只有一个字节的情况
            if (_stm32RxBuffer[0] == PKT_SOF) return; // 回到 Protocol A

            _stm32RxBuffer.erase(_stm32RxBuffer.begin());
        }

        // 需要 18 字节
        if (_stm32RxBuffer.size() >= sizeof(RuntimeFeedback)) {
            RuntimeFeedback fb;
            memcpy(&fb, _stm32RxBuffer.data(), sizeof(RuntimeFeedback));
            if (fb.isValid()) {
                // 封装为 TELEMETRY 包发给手机
                // Payload 为除了 start 和 checksum 之外的 14 个字节
                uint8_t payload[14];
                memcpy(payload, &fb.cmd1, 14);
                
                sendResponseToAdapters(static_cast<uint8_t>(CmdId::TELEMETRY), payload, 14);
            } else {
                Serial.println("[HC] Protocol B checksum err!");
            }
            _stm32RxBuffer.erase(_stm32RxBuffer.begin(), _stm32RxBuffer.begin() + sizeof(RuntimeFeedback));
        }
    }

    // --------------------------------------------------------
    // 发送工具
    // --------------------------------------------------------
    void sendResponseToAdapters(uint8_t cmd, const uint8_t* payload, uint16_t len) {
        uint8_t frame[6 + MAX_PAYLOAD_LEN];
        size_t frameLen = build_frame(cmd, payload, len, frame, sizeof(frame));
        if (frameLen > 0) {
            broadcastToAdapters(frame, frameLen);
        }
    }

    void broadcastToAdapters(const uint8_t* data, size_t len) {
        for (auto* adapter : _adapters) {
            adapter->send(data, len);
        }
    }

    uint32_t                _lastHeartbeat = 0;
    RuntimeCmd              _currentDrive;
    HardwareSerial&         _uart;
    int                     _txPin, _rxPin;
    uint32_t                _baud;
    StateMachine            _sm;
    std::vector<IAdapter*>  _adapters;
    std::vector<uint8_t>    _rxBuffer;
    std::vector<uint8_t>    _stm32RxBuffer;
};
