/**
 * ESP32 平衡车智能中间控制器
 * ============================
 * 通过 BLE (Nordic UART Service) 或 WiFi (HTTP) 接收指令和固件，
 * 经安全状态机过滤后通过 UART 转发给 STM32 Bootloader/主固件。
 *
 * 硬件接线:
 *   ESP32 GPIO17 (TX2) -> STM32 PB11 (USART3 RX)
 *   ESP32 GPIO16 (RX2) -> STM32 PB10 (USART3 TX)
 *   公共 GND 必须连接
 */

#include <Arduino.h>
#include "AdapterFactory.h"
#include "HoverConnector.h"
#include "WiFiAdapter.h"

// ============================================================
// ⚙️ 用户配置区 — 按实际情况修改
// ============================================================
#define ADAPTER_MODE      0       // 0 = BLE, 1 = WiFi

#define BLE_DEVICE_NAME   "HoverBoard-OTA"

#define WIFI_SSID         "YourSSID"
#define WIFI_PASSWORD     "YourPassword"
#define WIFI_PORT         80

// STM32 连接引脚 (ESP32 UART2)
#define STM32_TX_PIN      17      // ESP32 TX -> STM32 RX (PB11)
#define STM32_RX_PIN      16      // ESP32 RX -> STM32 TX (PB10)
#define STM32_BAUD        115200
// ============================================================

HoverConnector connector(Serial2, STM32_TX_PIN, STM32_RX_PIN, STM32_BAUD);

#if ADAPTER_MODE == 1
WiFiAdapter* wifiAdapter = nullptr;
#endif

void setup() {
    Serial.begin(115200);
    delay(500);

    Serial.println("\n========================================");
    Serial.println("  Hoverboard ESP32 Connector v1.0");
    Serial.println("========================================");

    // 初始化 STM32 UART
    connector.begin();

    // 通过工厂创建适配器
#if ADAPTER_MODE == 0
    Serial.println("[MAIN] Mode: BLE");
    IAdapter* adapter = AdapterFactory::create(AdapterType::BLE, BLE_DEVICE_NAME);
    if (!adapter) {
        Serial.println("[MAIN] ERROR: BLE adapter creation failed!");
        while (1) delay(1000);
    }
    connector.addAdapter(adapter);

#else
    Serial.println("[MAIN] Mode: WiFi");
    WiFiConfig cfg = { WIFI_SSID, WIFI_PASSWORD, WIFI_PORT };
    wifiAdapter = static_cast<WiFiAdapter*>(AdapterFactory::create(AdapterType::WIFI, nullptr, cfg));
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

#if ADAPTER_MODE == 1
    // WiFi 适配器需要在 loop() 中处理 HTTP 请求
    if (wifiAdapter) wifiAdapter->handle();
#endif

    // 定期打印系统状态 (每 5 秒)
    static uint32_t lastPrint = 0;
    if (millis() - lastPrint > 5000) {
        lastPrint = millis();
        Serial.printf("[MAIN] State: %s | Uptime: %lus\n",
                      connector.stateMachine().stateStr(),
                      millis() / 1000);
    }
}
