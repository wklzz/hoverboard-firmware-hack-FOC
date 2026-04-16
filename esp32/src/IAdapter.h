#pragma once
#include <cstdint>
#include <functional>

// ============================================================
// 通讯适配器抽象接口 (Factory Pattern 的抽象产品)
// ============================================================

class IAdapter {
public:
    virtual ~IAdapter() = default;

    // 初始化物理通道
    virtual bool init() = 0;

    // 发送原始字节
    virtual void send(const uint8_t* data, size_t len) = 0;

    // 关闭通道
    virtual void stop() = 0;

    // 物理层收到数据时触发 (上层绑定)
    std::function<void(const uint8_t*, size_t)> onRawData;
};
