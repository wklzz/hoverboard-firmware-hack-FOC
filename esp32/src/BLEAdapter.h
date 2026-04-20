#pragma once
#include <Arduino.h>
#include <NimBLEDevice.h>
#include <NimBLEServer.h>
#include <NimBLEUtils.h>
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

class BLEAdapter : public IAdapter, public NimBLEServerCallbacks, public NimBLECharacteristicCallbacks, public NimBLESecurityCallbacks {
public:
    explicit BLEAdapter(const char* deviceName = "HoverBoard-OTA")
        : _deviceName(deviceName) {}

    bool init() override {
        NimBLEDevice::init(_deviceName);
        NimBLEDevice::setMTU(517);

        // --- 开启底层配对加密 (动态 Passkey) ---
        NimBLEDevice::setSecurityAuth(true, true, true); // Bonding, MITM, Secure Connections
        NimBLEDevice::setSecurityIOCap(BLE_HS_IO_DISPLAY_ONLY); // 声明为“带显示屏的设备”
        NimBLEDevice::setSecurityCallbacks(this);        // 设置安全回调

        _server = NimBLEDevice::createServer();
        _server->setCallbacks(this);

        NimBLEService* service = _server->createService(NUS_SERVICE_UUID);

        _txChar = service->createCharacteristic(NUS_TX_UUID,
            NIMBLE_PROPERTY::NOTIFY);

        _rxChar = service->createCharacteristic(NUS_RX_UUID,
            NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR | NIMBLE_PROPERTY::WRITE_ENC);
        _rxChar->setCallbacks(this);

        service->start();

        NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
        adv->addServiceUUID(NUS_SERVICE_UUID);
        adv->setScanResponse(true);
        adv->setMinPreferred(0x06);
        NimBLEDevice::startAdvertising();

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
        NimBLEDevice::stopAdvertising();
    }

    // 协议处理
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

        // 转发到上层 HoverConnector
        if (onRawData) {
            onRawData(localBuf.data(), localBuf.size());
        }
    }

    bool isConnected() const { return _connected; }

private:

    // NimBLEServerCallbacks
    void onConnect(NimBLEServer* pServer) override {
        _connected = true;
        // 注意：这里仍然在回调中，但 stopAdvertising 通常较快且不涉及协议交互
        NimBLEDevice::stopAdvertising();
        Serial.println("[BLE] Client connected, advertising stopped.");
    }

    void onDisconnect(NimBLEServer* pServer) override {
        _connected = false;
        _disconnectionPending = true; 
        Serial.println("[BLE] Client disconnected, restarting advertising...");
        NimBLEDevice::startAdvertising();
    }

    // NimBLECharacteristicCallbacks (RX)
    void onWrite(NimBLECharacteristic* pChar) override {
        std::string val = pChar->getValue();
        if (val.empty()) return;

        // 仅仅将数据推入缓冲区，不在回调中做任何复杂计算或 Serial 操作
        std::lock_guard<std::mutex> lock(_bufferMutex);
        _incomingData.insert(_incomingData.end(), val.begin(), val.end());
    }

    // NimBLESecurityCallbacks
    uint32_t onPassKeyRequest() override {
        return 0; 
    }

    void onPassKeyNotify(uint32_t passkey) override {
        Serial.println("*********************************");
        Serial.printf("  蓝牙配对码: %06u  \n", passkey);
        Serial.println("*********************************");
    }

    bool onSecurityRequest() override {
        return true;
    }

    void onAuthenticationComplete(ble_gap_conn_desc* desc) override {
        if (desc->sec_state.encrypted) {
            Serial.println("[BLE] 认证成功：已建立安全连接");
        } else {
            Serial.println("[BLE] 认证失败：连接未加密");
        }
    }

    bool onConfirmPIN(uint32_t passkey) override {
        return true;
    }

    const char*           _deviceName;
    NimBLEServer*            _server  = nullptr;
    NimBLECharacteristic*    _txChar  = nullptr;
    NimBLECharacteristic*    _rxChar  = nullptr;
    volatile bool         _connected = false;
    volatile bool         _disconnectionPending = false;

    std::vector<uint8_t>  _incomingData;
    std::mutex            _bufferMutex;
};
