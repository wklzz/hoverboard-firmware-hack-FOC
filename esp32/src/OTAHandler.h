#pragma once
#include <Arduino.h>
#include <Update.h>
#include "protocol.h"

class OTAHandler {
public:
    enum class OTAStatus {
        IDLE,
        UPDATING,
        FINISHED,
        ERROR
    };

    OTAHandler() : _status(OTAStatus::IDLE) {}

    bool begin(uint32_t totalSize) {
        if (_status == OTAStatus::UPDATING) return false;
        
        Serial.printf("[OTA] Starting OTA for ESP32. Size: %u bytes\n", totalSize);
        if (!Update.begin(totalSize)) {
            Update.printError(Serial);
            _status = OTAStatus::ERROR;
            return false;
        }
        
        _status = OTAStatus::UPDATING;
        _totalSize = totalSize;
        _bytesReceived = 0;
        return true;
    }

    bool handleData(uint32_t offset, const uint8_t* data, size_t len) {
        if (_status != OTAStatus::UPDATING) return false;
        
        // 我们目前只支持顺序写入以简化逻辑
        if (offset != _bytesReceived) {
            Serial.printf("[OTA] Offset mismatch! Expected %u, got %u\n", _bytesReceived, offset);
            return false;
        }

        size_t written = Update.write(const_cast<uint8_t*>(data), len);
        if (written != len) {
            Update.printError(Serial);
            _status = OTAStatus::ERROR;
            return false;
        }

        _bytesReceived += len;
        // Serial.printf("[OTA] Progress: %u/%u\n", _bytesReceived, _totalSize);
        return true;
    }

    bool end() {
        if (_status != OTAStatus::UPDATING) return false;

        if (Update.end(true)) { // true means install on next boot
            Serial.println("[OTA] Update Success! Rebooting...");
            _status = OTAStatus::FINISHED;
            return true;
        } else {
            Update.printError(Serial);
            _status = OTAStatus::ERROR;
            return false;
        }
    }

    OTAStatus status() const { return _status; }
    uint32_t bytesReceived() const { return _bytesReceived; }

private:
    OTAStatus _status;
    uint32_t _totalSize = 0;
    uint32_t _bytesReceived = 0;
};
