#pragma once
#include <Arduino.h>
#include "IAdapter.h"

// ============================================================
// Serial 适配器
// 直接复用 USB 串口进行控制 (用于 test.py 或 PC 调试)
// ============================================================

class SerialAdapter : public IAdapter {
public:
    explicit SerialAdapter(Stream& stream) : _stream(stream) {}

    bool init() override {
        // Serial 已经在 main.cpp 中初始化过
        return true;
    }

    void send(const uint8_t* data, size_t len) override {
        _stream.write(data, len);
    }

    void stop() override {
        // Serial 通常不关闭
    }

    // 需在 loop() 中被调用
    void handle() override {
        if (_stream.available()) {
            uint8_t buf[64];
            size_t n = _stream.readBytes(buf, sizeof(buf));
            if (n > 0 && onRawData) {
                onRawData(buf, n);
            }
        }
    }

private:
    Stream& _stream;
};
