#pragma once
#include <Arduino.h>
#include "protocol.h"

// ============================================================
// 系统安全状态机
// 保证平衡车在不同运行阶段只接受合法指令
// ============================================================

enum class SystemState {
    IDLE,       // 已上电，未连接
    CONNECTED,  // 已连接，等待指令
    RUNNING,    // 正在驱动电机
    OTA,        // 固件升级中 — 屏蔽控制指令
    ALARM,      // 故障 — 强制停机
};

class StateMachine {
public:
    SystemState current = SystemState::IDLE;

    // 检查当前状态下是否允许执行此命令
    // 返回 true = 允许, false = 拒绝
    bool canExecute(uint8_t cmdId) {
        CmdId id = static_cast<CmdId>(cmdId & ~0x80);

        // CONFIG, STATUS 不触及底层硬件，一般在非 ALARM 和非 OTA 状态下均可
        if (id == CmdId::CONFIG || id == CmdId::STATUS) {
            return current != SystemState::ALARM && current != SystemState::OTA;
        }

        switch (current) {
            case SystemState::RUNNING:
                // 运行中禁止 ERASE / WRITE / BOOT / PING (透传给Bootloader的)
                if (id == CmdId::ERASE || id == CmdId::WRITE || id == CmdId::BOOT) {
                    Serial.println("[SM] BLOCKED: OTA cmd while RUNNING");
                    return false;
                }
                break;

            case SystemState::ALARM:
                // 故障状态只允许 PING / STATUS (PC/手机获取状态)
                if (id != CmdId::PING && id != CmdId::STATUS) {
                    Serial.println("[SM] BLOCKED: non-PING cmd while ALARM");
                    return false;
                }
                break;

            case SystemState::OTA:
                // OTA 中不允许控制指令 (在此处屏蔽 DRIVE)
                if (id == CmdId::DRIVE) {
                    Serial.println("[SM] BLOCKED: DRIVE cmd while OTA");
                    return false;
                }
                break;

            default:
                break;
        }
        return true;
    }

    // 根据执行成功的命令进行状态转换
    void onCommandExecuted(uint8_t cmdId) {
        CmdId id = static_cast<CmdId>(cmdId & ~0x80);
        switch (id) {
            case CmdId::ERASE:
                current = SystemState::OTA;
                Serial.println("[SM] -> OTA");
                break;
            case CmdId::BOOT:
                current = SystemState::IDLE;
                Serial.println("[SM] -> IDLE (App booted)");
                break;
            case CmdId::PING:
                if (current == SystemState::IDLE) {
                    current = SystemState::CONNECTED;
                    Serial.println("[SM] -> CONNECTED");
                }
                break;
            case CmdId::DRIVE:
                if (current == SystemState::CONNECTED || current == SystemState::IDLE) {
                    current = SystemState::RUNNING;
                    Serial.println("[SM] -> RUNNING");
                }
                break;
            default:
                break;
        }
    }

    void triggerAlarm() {
        current = SystemState::ALARM;
        Serial.println("[SM] -> ALARM");
    }

    void reset() {
        current = SystemState::IDLE;
        Serial.println("[SM] -> IDLE (reset)");
    }

    const char* stateStr() const {
        switch (current) {
            case SystemState::IDLE:      return "IDLE";
            case SystemState::CONNECTED: return "CONNECTED";
            case SystemState::RUNNING:   return "RUNNING";
            case SystemState::OTA:       return "OTA";
            case SystemState::ALARM:     return "ALARM";
        }
        return "UNKNOWN";
    }
};
