#pragma once
#include "IAdapter.h"
#include "BLEAdapter.h"
#include "WiFiAdapter.h"
#include "PwmAdapter.h"

// ============================================================
// 适配器工厂 (Factory Pattern)
// 上层通过工厂获取适配器，对具体实现完全透明
// ============================================================

enum class AdapterType {
    BLE,
    WIFI,
    PWM
};

struct WiFiConfig {
    const char* ssid     = nullptr;
    const char* password = nullptr;
    uint16_t    port     = 80;
};

class AdapterFactory {
public:
    // 创建 BLE 适配器
    static IAdapter* create(AdapterType type, const char* bleName = "HoverBoard-OTA",
                             WiFiConfig wifiCfg = {}) {
        switch (type) {
            case AdapterType::BLE:
                return new BLEAdapter(bleName);
            case AdapterType::WIFI:
                if (!wifiCfg.ssid) return nullptr;
                return new WiFiAdapter(wifiCfg.ssid, wifiCfg.password, wifiCfg.port);
            case AdapterType::PWM:
                return new PwmAdapter();
            default:
                return nullptr;
        }
    }
};

