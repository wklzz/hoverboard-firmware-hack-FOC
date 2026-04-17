/**
 * ESP32 平衡车智能中间控制器
 * ============================
 * 通过 BLE (Nordic UART Service) 或 WiFi (HTTP) 接收指令和固件，
 * 经安全状态机过滤后通过 UART 转发给 STM32 Bootloader/主固件。
 */

#include <Arduino.h>
#include <Preferences.h>
#include "AdapterFactory.h"
#include "HoverConnector.h"
#include "PwmAdapter.h"
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

// STM32 连接引脚 (使用安全 GPIO 避开 PSRAM/USB 占用)
#define STM32_TX_PIN      4       // ESP32 TX -> STM32 RX (PB11)
#define STM32_RX_PIN      5       // ESP32 RX -> STM32 TX (PB10)
#define STM32_BAUD        115200
// ============================================================

HoverConnector connector(Serial1, STM32_TX_PIN, STM32_RX_PIN, STM32_BAUD);
SerialAdapter  serialAdapter(Serial);

IAdapter*      mainAdapter = nullptr;
Preferences    prefs;
uint8_t        ctrlMode = 0; // 0 = BLE, 1 = PWM

#if ADAPTER_MODE == 1
WiFiAdapter*   wifiAdapter = nullptr;
#endif
PwmAdapter*    pwmAdapterGlobal = nullptr;

void setup() {
    Serial.begin(115200);
    Serial.println(">>> ESP32 BOOTING <<<");
    Serial.flush();
    delay(1000);

    // 加载用户模式设置
    prefs.begin("hover", false);
    ctrlMode = prefs.getUChar("mode", 0);

    Serial.println("\n========================================");
    Serial.println("  Hoverboard ESP32 Connector v1.0");
    Serial.printf("  Current Mode: %d (0=BLE/WiFi, 1=PWM)\n", ctrlMode);
    Serial.println("========================================");

    // 初始化 STM32 UART
    connector.onStatusRequest = [](uint8_t* payload, uint16_t maxLen) {
        if (maxLen >= 1) {
            payload[0] = (uint8_t)ctrlMode;
            Serial.printf("[MAIN] Reporting mode: %d\n", ctrlMode);
            return (uint16_t)1;
        }
        return (uint16_t)0;
    };

    connector.begin();

    // 始终开启串口控制（用于本地测试/调试）
    connector.addAdapter(&serialAdapter);

    // 始终开启 PWM 控制适配器 (使用 6/7 号引脚)
    pwmAdapterGlobal = new PwmAdapter(6, 7);
    if (pwmAdapterGlobal) {
        pwmAdapterGlobal->setEnabled(ctrlMode == 1);
        connector.addAdapter(pwmAdapterGlobal);
        Serial.printf("[MAIN] PWM Adapter initialized on pins 6/7 (%s).\n", (ctrlMode == 1) ? "Enabled" : "Disabled");
    }

    // 设置在运行时动态切换模式的回调
    connector.onConfig = [](uint8_t key, const uint8_t* payload, uint16_t len) {
        if (key == 0x01 && len >= 1) {
            ctrlMode = payload[0];
            prefs.putUChar("mode", ctrlMode);
            Serial.printf("[MAIN] Mode switched to %d via Config Command\n", ctrlMode);
            if (pwmAdapterGlobal) {
                pwmAdapterGlobal->setEnabled(ctrlMode == 1);
            }
        }
    };

    // 通过工厂创建主要适配器 (BLE 或 WiFi)
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

    // 处理 PWM 适配器
    if (pwmAdapterGlobal) {
        pwmAdapterGlobal->handle();
    }

    // 定期打印系统状态 (每 1 秒)
    static uint32_t lastPrint = 0;
    if (millis() - lastPrint > 1000) {
        lastPrint = millis();
        Serial.print("[STATUS] Mode: "); Serial.print(ctrlMode);
        Serial.print(" ("); Serial.print(ctrlMode == 1 ? "PWM" : "BLE/WiFi");
        Serial.print(") | State: "); Serial.print(connector.stateMachine().stateStr());
        Serial.print(" | Uptime: "); Serial.print(millis() / 1000); Serial.print("s");
        
        if (pwmAdapterGlobal) {
            Serial.print(" | PWM CH1: "); Serial.print(pwmAdapterGlobal->getCh1());
            Serial.print(" CH2: "); Serial.print(pwmAdapterGlobal->getCh2());
            Serial.print(pwmAdapterGlobal->isTimeout() ? " [TIMEOUT]" : " [OK]");
        }
        Serial.println();
        Serial.flush();
    }
}
