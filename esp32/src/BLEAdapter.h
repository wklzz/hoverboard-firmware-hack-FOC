#pragma once
#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <vector>
#include <mutex>
#include "IAdapter.h"
#include "protocol.h"

// ============================================================
// BLE 适配器 (使用 Nordic UART Service 标准 UUID)
// 包含动态认证：连接后停止广播，通过认证才允许控制，断开恢复广播
// 堆栈保护版：所有的协议解析和回复均在 handle() 中处理，不在回调执行
// ============================================================

// Nordic UART Service UUIDs
static constexpr char NUS_SERVICE_UUID[]  = "6E400001-B5A3-F393-E0A9-E50E24DCCA9E";
static constexpr char NUS_RX_UUID[]       = "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"; // Write
static constexpr char NUS_TX_UUID[]       = "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"; // Notify

class BLEAdapter : public IAdapter, public BLEServerCallbacks, public BLECharacteristicCallbacks {
public:
    explicit BLEAdapter(const char* deviceName = "HoverBoard-OTA")
        : _deviceName(deviceName) {}

    bool init() override {
        BLEDevice::init(_deviceName);
        BLEDevice::setMTU(517);

        _server = BLEDevice::createServer();
        _server->setCallbacks(this);

        BLEService* service = _server->createService(NUS_SERVICE_UUID);

        _txChar = service->createCharacteristic(NUS_TX_UUID,
            BLECharacteristic::PROPERTY_NOTIFY);
        _txChar->addDescriptor(new BLE2902());

        _rxChar = service->createCharacteristic(NUS_RX_UUID,
            BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
        _rxChar->setCallbacks(this);

        service->start();

        BLEAdvertising* adv = BLEDevice::getAdvertising();
        adv->addServiceUUID(NUS_SERVICE_UUID);
        adv->setScanResponse(true);
        adv->setMinPreferred(0x06);
        BLEDevice::startAdvertising();

        Serial.println("[BLE] Advertising started: " + String(_deviceName));
        return true;
    }

    void send(const uint8_t* data, size_t len) override {
        // 这里的 send 可能由 HC (HoverConnector) 调用，
        // HC 的 processBuffer 在 handle() 中运行，所以是安全的
        if (!_connected || !_txChar) return;
        _txChar->setValue(const_cast<uint8_t*>(data), len);
        _txChar->notify();
    }

    void stop() override {
        BLEDevice::stopAdvertising();
    }

    // 所有的协议逻辑迁移到这里
    void handle() override {
        if (_disconnectionPending) {
            _disconnectionPending = false;
            if (onConnectionLost) onConnectionLost();
        }

        std::vector<uint8_t> localBuf;
        {
            std::lock_guard<std::mutex> lock(_bufferMutex);
            if (!_incomingData.empty()) {
                localBuf = std::move(_incomingData);
                _incomingData.clear();
            }
        }

        if (localBuf.empty()) return;

        const uint8_t* data = localBuf.data();
        size_t len = localBuf.size();

        if (!_authenticated) {
            // 解析是否为相关的认证包
            if (len >= 6 && data[0] == PKT_SOF) {
                if (data[1] == static_cast<uint8_t>(CmdId::AUTH_REQ)) {
                    sendChallenge();
                    return;
                }
                else if (data[1] == static_cast<uint8_t>(CmdId::AUTH_RES)) {
                    uint16_t pLen = (uint8_t)data[2] | ((uint8_t)data[3] << 8);
                    if (pLen == 4 && len >= 6 + 4) {
                        uint32_t response = (uint8_t)data[4] | ((uint8_t)data[5] << 8) | ((uint8_t)data[6] << 16) | ((uint8_t)data[7] << 24);
                        if (response == (_challenge ^ 0x12345678)) {
                            _authenticated = true;
                            Serial.println("[BLE] Auth SUCCESS! Control opened.");
                            
                            uint8_t out[6];
                            size_t outLen = build_frame(static_cast<uint8_t>(CmdId::AUTH_RES) | ACK_MASK, nullptr, 0, out, sizeof(out));
                            send(out, outLen);
                            return;
                        }
                    }
                }
            }
            Serial.println("[BLE] Auth FAILED or Unknown packet. Dropping.");
            return;
        }

        // 认证通过，转发到上层 HoverConnector
        if (onRawData) {
            onRawData(data, len);
        }
    }

    bool isConnected() const { return _connected; }

private:
    void sendChallenge() {
        _challenge = esp_random();
        uint8_t payload[4];
        payload[0] = (uint8_t)(_challenge & 0xFF);
        payload[1] = (uint8_t)((_challenge >> 8) & 0xFF);
        payload[2] = (uint8_t)((_challenge >> 16) & 0xFF);
        payload[3] = (uint8_t)((_challenge >> 24) & 0xFF);

        uint8_t frame[10];
        size_t len = build_frame(static_cast<uint8_t>(CmdId::AUTH_REQ), payload, 4, frame, sizeof(frame));
        if (len > 0) {
            send(frame, len); // 内部已通过 handle 主线程调用
            Serial.printf("[BLE] Sent AUTH_REQ challenge: 0x%08X\n", _challenge);
        }
    }

    // BLEServerCallbacks
    void onConnect(BLEServer* pServer) override {
        _connected = true;
        _authenticated = false;
        // 注意：这里仍然在回调中，但 stopAdvertising 通常较快且不涉及协议交互
        BLEDevice::stopAdvertising();
        Serial.println("[BLE] Client connected, advertising stopped.");
    }

    void onDisconnect(BLEServer* pServer) override {
        _connected = false;
        _authenticated = false;
        _disconnectionPending = true; 
        Serial.println("[BLE] Client disconnected, restarting advertising...");
        BLEDevice::startAdvertising();
    }

    // BLECharacteristicCallbacks (RX)
    void onWrite(BLECharacteristic* pChar) override {
        std::string val = pChar->getValue();
        if (val.empty()) return;

        // 仅仅将数据推入缓冲区，不在回调中做任何复杂计算或 Serial 操作
        std::lock_guard<std::mutex> lock(_bufferMutex);
        _incomingData.insert(_incomingData.end(), val.begin(), val.end());
    }

    const char*           _deviceName;
    BLEServer*            _server  = nullptr;
    BLECharacteristic*    _txChar  = nullptr;
    BLECharacteristic*    _rxChar  = nullptr;
    volatile bool         _connected = false;
    volatile bool         _disconnectionPending = false;
    bool                  _authenticated = false;
    uint32_t              _challenge = 0;

    std::vector<uint8_t>  _incomingData;
    std::mutex            _bufferMutex;
};
