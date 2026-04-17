/**
 * ESP32 平衡车智能中间控制器
 * ============================
 * 通过 BLE (Nordic UART Service) 或 WiFi (HTTP) 接收指令和固件，
 * 经安全状态机过滤后通过 UART 转发给 STM32 Bootloader/主固件。
 */

#include <Arduino.h>
#include "AdapterFactory.h"
#include "HoverConnector.h"
#include "WiFiAdapter.h"
#include "SerialAdapter.h"

// ============================================================
// ⚙️ 用户配置区 — 按实际情况修改
// ============================================================
#define ADAPTER_MODE      0       // 0 = BLE, 1 = WiFi

#define BLE_DEVICE_NAME   "HoverBoard-OTA"

#define WIFI_SSID         "YourSSID"
#define WIFI_PASSWORD     "YourPassword"
#define WIFI_PORT         80

// STM32 连接引脚 (ESP32-S3 UART1/2, 使用 GPIO 17/16 匹配 README)
#define STM32_TX_PIN      17      // ESP32 TX -> STM32 RX (PB11)
#define STM32_RX_PIN      16      // ESP32 RX -> STM32 TX (PB10)
#define STM32_BAUD        115200
// ============================================================

HoverConnector connector(Serial2, STM32_TX_PIN, STM32_RX_PIN, STM32_BAUD);
SerialAdapter  serialAdapter(Serial);

IAdapter*      mainAdapter = nullptr;

#if ADAPTER_MODE == 1
WiFiAdapter*   wifiAdapter = nullptr;
#endif

void setup() {
    Serial.begin(115200);
    delay(500);

    Serial.println("\n========================================");
    Serial.println("  Hoverboard ESP32 Connector v1.0");
    Serial.println("========================================");

    // 初始化 STM32 UART
    connector.begin();

    // 始终开启串口控制（用于本地测试/调试）
    connector.addAdapter(&serialAdapter);

    // 通过工厂创建适配器
#if ADAPTER_MODE == 0
    Serial.println("[MAIN] Mode: BLE");
    mainAdapter = AdapterFactory::create(AdapterType::BLE, BLE_DEVICE_NAME);
    if (!mainAdapter) {
        Serial.println("[MAIN] ERROR: BLE adapter creation failed!");
        while (1) delay(1000);
    }
    connector.addAdapter(mainAdapter);

#else
    Serial.println("[MAIN] Mode: WiFi");
    WiFiConfig cfg = { WIFI_SSID, WIFI_PASSWORD, WIFI_PORT };
    wifiAdapter = static_cast<WiFiAdapter*>(AdapterFactory::create(AdapterType::WIFI, nullptr, cfg));
    mainAdapter = wifiAdapter;
    if (!wifiAdapter) {
        Serial.println("[MAIN] ERROR: WiFi adapter creation failed!");
        while (1) delay(1000);
    }
    connector.addAdapter(wifiAdapter);
#endif

    Serial.println("[MAIN] Setup complete. Waiting for connection...");
}

void loop() {
    // 处理 STM32 的回包并转发给适配器
    connector.update();

    // 处理来自 USB 串口的消息 (始终存在的本地通道)
    serialAdapter.handle();

    // 处理主要适配器 (BLE 或 WiFi)
    if (mainAdapter) {
        mainAdapter->handle();
    }

    // 定期打印系统状态 (每 5 秒)
    static uint32_t lastPrint = 0;
    if (millis() - lastPrint > 5000) {
        lastPrint = millis();
        Serial.printf("[MAIN] State: %s | Uptime: %lus\n",
                      connector.stateMachine().stateStr(),
                      millis() / 1000);
    }
}
