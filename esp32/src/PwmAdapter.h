#pragma once
#include <Arduino.h>
#include "IAdapter.h"
#include "protocol.h"

// ============================================================
// PWM 通讯适配器
// 通过 attachInterruptArg 测量两个引脚的 PWM 脉宽，
// 映射为 Protocol C 的 DRIVE 帧投递给 HoverConnector。
//
// 特性：
// - 中位死区过滤
// - 1000us~2000us 映射至 -1000~1000
// - 500ms 信号丢失超时保护（且无信号时不干扰其他适配器工作）
// ============================================================

class PwmAdapter : public IAdapter {
private:
    struct PwmContext {
        uint8_t pin;
        volatile uint32_t startTime = 0;
        volatile uint16_t pulseWidth = 0;
        volatile uint32_t lastUpdate = 0;
    };

    PwmContext _ctxSteer;
    PwmContext _ctxSpeed;

    uint32_t _lastFrameTime = 0;
    bool     _wasActive     = false;
    bool     _enabled       = true;
    uint32_t _lastDebugPrint = 0;

    // ESP32 中断处理函数
    static void isrHandler(void* arg) {
        PwmContext* ctx = static_cast<PwmContext*>(arg);
        if (digitalRead(ctx->pin) == HIGH) {
            ctx->startTime = micros();
        } else {
            if (ctx->startTime != 0) {
                uint32_t width = micros() - ctx->startTime;
                ctx->pulseWidth = width;
                ctx->lastUpdate = millis();
            }
        }
    }

    // 辅助函数：构造并发送 Protocol A/C 格式的 DRIVE 指令给连接器
    void sendDriveCommand(int16_t steer, int16_t speed) {
        if (!onRawData) return;
        
        uint8_t payload[4];
        payload[0] = steer & 0xFF;
        payload[1] = (steer >> 8) & 0xFF;
        payload[2] = speed & 0xFF;
        payload[3] = (speed >> 8) & 0xFF;

        uint8_t frame[16];
        size_t len = build_frame((uint8_t)CmdId::DRIVE, payload, 4, frame, sizeof(frame));
        if (len > 0) {
            // 调用回调，这会将协议包塞入到 HoverConnector 的 processBuffer 中
            onRawData(frame, len);
        }
    }

public:
    PwmAdapter(uint8_t pinSteer = 18, uint8_t pinSpeed = 19) {
        _ctxSteer.pin = pinSteer;
        _ctxSpeed.pin = pinSpeed;
    }

    void setEnabled(bool en) {
        _enabled = en;
        if (!en && _wasActive) {
            // 被禁用时，如果之前在活动状态，发一次零速指令彻底释放主控
            _wasActive = false;
            sendDriveCommand(0, 0);
            Serial.println("[PWM] Disabled by mode switch. Stopped motors.");
        }
    }

    uint16_t getCh1() const { return _ctxSteer.pulseWidth; }
    uint16_t getCh2() const { return _ctxSpeed.pulseWidth; }
    
    bool isTimeout() const {
        uint32_t now = millis();
        bool steerActive = (now - _ctxSteer.lastUpdate <= 500) && (_ctxSteer.pulseWidth > 0);
        bool speedActive = (now - _ctxSpeed.lastUpdate <= 500) && (_ctxSpeed.pulseWidth > 0);
        return !(steerActive || speedActive);
    }

    bool init() override {
        pinMode(_ctxSteer.pin, INPUT_PULLDOWN);
        pinMode(_ctxSpeed.pin, INPUT_PULLDOWN);
        attachInterruptArg(digitalPinToInterrupt(_ctxSteer.pin), isrHandler, &_ctxSteer, CHANGE);
        attachInterruptArg(digitalPinToInterrupt(_ctxSpeed.pin), isrHandler, &_ctxSpeed, CHANGE);
        Serial.printf("[PWM] Initialized on pins: Steer=%d, Speed=%d\n", _ctxSteer.pin, _ctxSpeed.pin);
        return true;
    }

    void send(const uint8_t* data, size_t len) override {
        // PWM 是单向接收适配器，不需要往接收机回复数据
    }

    void stop() override {
        detachInterrupt(digitalPinToInterrupt(_ctxSteer.pin));
        detachInterrupt(digitalPinToInterrupt(_ctxSpeed.pin));
        Serial.println("[PWM] Stopped.");
    }

    void handle() override {
        if (!_enabled) return;

        uint32_t now = millis();
        // 设置 50Hz (20ms) 刷新率投递给 STM32
        if (now - _lastFrameTime < 20) return;
        _lastFrameTime = now;

        // 验证超时
        bool steerActive = (now - _ctxSteer.lastUpdate <= 500) && (_ctxSteer.pulseWidth > 0);
        bool speedActive = (now - _ctxSpeed.lastUpdate <= 500) && (_ctxSpeed.pulseWidth > 0);
        
        bool active = steerActive || speedActive;

        // 无连接时不发送，防止干扰 BLE 或 WiFi 指令
        if (!active) {
            if (_wasActive) {
                // 刚刚失去连接，发送 0 停止电机
                _wasActive = false;
                sendDriveCommand(0, 0);
                if (onConnectionLost) onConnectionLost();
                Serial.println("[PWM] Signal lost. Sent fallback stop command.");
            }
            return;
        }

        _wasActive = true;

        const int16_t deadband = 150;

        int16_t steerCmd = 0;
        if (steerActive) {
            uint16_t width = _ctxSteer.pulseWidth;
            if (width < 1000) width = 1000;
            if (width > 2000) width = 2000;
            int16_t raw = (int16_t)((width - 1500) * 2);
            if (abs(raw) < deadband) {
                steerCmd = 0;
            } else {
                if (raw > 0) steerCmd = map(raw, deadband, 1000, 0, 1000);
                else steerCmd = map(raw, -1000, -deadband, -1000, 0);
            }
        }

        int16_t speedCmd = 0;
        if (speedActive) {
            uint16_t width = _ctxSpeed.pulseWidth;
            if (width < 1000) width = 1000;
            if (width > 2000) width = 2000;
            int16_t raw = (int16_t)((width - 1500) * 2);
            if (abs(raw) < deadband) {
                speedCmd = 0;
            } else {
                if (raw > 0) speedCmd = map(raw, deadband, 1000, 0, 1000);
                else speedCmd = map(raw, -1000, -deadband, -1000, 0);
            }
        }

        sendDriveCommand(steerCmd, speedCmd);

        // 每 500ms 打印一次调试信息，避免霸屏
        if (now - _lastDebugPrint > 500) {
            _lastDebugPrint = now;
            Serial.printf("[PWM-DEBUG] Raw CH1:%u CH2:%u -> Cmd Steer:%d Speed:%d\n",
                          _ctxSteer.pulseWidth, _ctxSpeed.pulseWidth, steerCmd, speedCmd);
        }
    }
};
