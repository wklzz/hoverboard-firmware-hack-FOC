#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include "IAdapter.h"

// ============================================================
// WiFi 适配器
// 提供 HTTP OTA 端点供局域网内直接传输固件
// POST /ota  — 上传 .bin 文件流，直接派发到上层回调
// GET  /ping — 心跳检测
// ============================================================

class WiFiAdapter : public IAdapter {
public:
    WiFiAdapter(const char* ssid, const char* password, uint16_t port = 80)
        : _ssid(ssid), _pass(password), _port(port), _server(port) {}

    bool init() override {
        WiFi.mode(WIFI_STA);
        WiFi.begin(_ssid, _pass);

        Serial.print("[WiFi] Connecting to: ");
        Serial.print(_ssid);

        uint8_t retries = 30;
        while (WiFi.status() != WL_CONNECTED && retries--) {
            delay(500);
            Serial.print('.');
        }

        if (WiFi.status() != WL_CONNECTED) {
            Serial.println("\n[WiFi] Connection FAILED");
            return false;
        }

        Serial.println("\n[WiFi] Connected. IP: " + WiFi.localIP().toString());

        // 注册 HTTP 路由
        _server.on("/ping", HTTP_GET, [this]() {
            _server.send(200, "text/plain", "PONG");
        });

        _server.on("/ota", HTTP_POST, [this]() {
            _server.send(200, "text/plain", "OTA_DONE");
        }, [this]() {
            // 分块上传处理
            HTTPUpload& upload = _server.upload();
            if (upload.status == UPLOAD_FILE_START) {
                Serial.printf("[WiFi] OTA Start: %s\n", upload.filename.c_str());
            } else if (upload.status == UPLOAD_FILE_WRITE) {
                if (onRawData) {
                    onRawData(upload.buf, upload.currentSize);
                }
            } else if (upload.status == UPLOAD_FILE_END) {
                Serial.printf("[WiFi] OTA Complete: %u bytes\n", upload.totalSize);
            }
        });

        _server.begin();
        Serial.printf("[WiFi] HTTP server started on port %d\n", _port);
        return true;
    }

    // 必须在 loop() 中调用
    void handle() { _server.handleClient(); }

    void send(const uint8_t* data, size_t len) override {
        // WiFi 适配器为接收方，此处预留用于向手机反馈状态
        // 实际反馈通过 HTTP 响应完成
        (void)data; (void)len;
    }

    void stop() override {
        _server.stop();
        WiFi.disconnect();
    }

private:
    const char* _ssid;
    const char* _pass;
    uint16_t    _port;
    WebServer   _server;
};
