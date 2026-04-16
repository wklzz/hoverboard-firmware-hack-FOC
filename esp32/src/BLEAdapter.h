#pragma once
#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include "IAdapter.h"

// ============================================================
// BLE 适配器 (使用 Nordic UART Service 标准 UUID)
// 手机/小程序可通过 BLE 发送 OTA 数据包和控制指令
// ============================================================

// Nordic UART Service UUIDs
static constexpr char NUS_SERVICE_UUID[]  = "6E400001-B5A3-F393-E0A9-E50E24DCCA9E";
static constexpr char NUS_RX_UUID[]       = "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"; // Write (手机 -> ESP32)
static constexpr char NUS_TX_UUID[]       = "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"; // Notify (ESP32 -> 手机)

class BLEAdapter : public IAdapter, public BLEServerCallbacks, public BLECharacteristicCallbacks {
public:
    explicit BLEAdapter(const char* deviceName = "HoverBoard-OTA")
        : _deviceName(deviceName) {}

    bool init() override {
        BLEDevice::init(_deviceName);
        BLEDevice::setMTU(517); // 请求最大 MTU 以提高吞吐量

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
        if (!_connected || !_txChar) return;
        _txChar->setValue(const_cast<uint8_t*>(data), len);
        _txChar->notify();
    }

    void stop() override {
        BLEDevice::stopAdvertising();
    }

    bool isConnected() const { return _connected; }

private:
    // BLEServerCallbacks
    void onConnect(BLEServer*) override {
        _connected = true;
        Serial.println("[BLE] Client connected");
    }
    void onDisconnect(BLEServer*) override {
        _connected = false;
        Serial.println("[BLE] Client disconnected, restarting advertising...");
        BLEDevice::startAdvertising();
    }

    // BLECharacteristicCallbacks (RX)
    void onWrite(BLECharacteristic* pChar) override {
        std::string val = pChar->getValue();
        if (!val.empty() && onRawData) {
            onRawData(reinterpret_cast<const uint8_t*>(val.data()), val.size());
        }
    }

    const char*           _deviceName;
    BLEServer*            _server  = nullptr;
    BLECharacteristic*    _txChar  = nullptr;
    BLECharacteristic*    _rxChar  = nullptr;
    bool                  _connected = false;
};
