/**
 * BLE Manager for Hoverboard
 */

import { buildFrame, CmdId } from './protocol.js';

const app = getApp();

class BLEManager {
  constructor() {
    this.deviceId = '';
    this.connected = false;
    this.onDataCallback = null;
    this.onStatusCallback = null;
    this.onAuthCallback = null;
    this.expectDisconnect = false; // 是否为主动断开
  }

  log(msg) {
    console.log('[BLE]', msg);
    if (this.onStatusCallback) this.onStatusCallback(msg);
  }

  init() {
    return new Promise((resolve, reject) => {
      wx.onBLEConnectionStateChange((res) => {
        this.log(`连接状态变化: ${res.connected ? '已连接' : '已断开'}`);
        const prevConnected = this.connected;
        this.connected = res.connected;

        // 如果是意外断开，触发重连逻辑
        if (prevConnected && !res.connected && !this.expectDisconnect) {
          this.handleAutoReconnect();
        }
      });

      wx.openBluetoothAdapter({
        success: (res) => {
          this.log('蓝牙适配器初始化成功');
          resolve(res);
        },
        fail: (err) => {
          this.log('请开启蓝牙并授权');
          reject(err);
        }
      });
    });
  }

  startScan(onDeviceFound) {
    this.log('开始扫描设备...');
    wx.startBluetoothDevicesDiscovery({
      allowDuplicatesKey: false,
      success: () => {
        wx.onBluetoothDeviceFound((res) => {
          res.devices.forEach(device => {
            if (device.name || device.localName) {
              onDeviceFound(device);
            }
          });
        });
      }
    });
  }

  stopScan() {
    wx.stopBluetoothDevicesDiscovery();
  }

  connect(deviceId) {
    this.deviceId = deviceId;
    this.expectDisconnect = false; // 重置预期断开标志
    this.log(`正在连接: ${deviceId}`);
    
    return new Promise((resolve, reject) => {
      wx.createBLEConnection({
        deviceId,
        success: () => {
          this.log('连接成功，正在发现服务...');
          this.setupServices(deviceId).then(resolve).catch(reject);
        },
        fail: reject
      });
    });
  }

  async setupServices(deviceId) {
    const serviceId = '6E400001-B5A3-F393-E0A9-E50E24DCCA9E';
    const readCharId = '6E400003-B5A3-F393-E0A9-E50E24DCCA9E';
    const writeCharId = '6E400002-B5A3-F393-E0A9-E50E24DCCA9E';

    // 请求更大的 MTU 以支持大包传输 (如 OTA)
    wx.setBLEMTU({
      deviceId,
      mtu: 512,
      success: (res) => {
        this.log(`MTU 设置成功: ${res.mtu}`);
      },
      fail: (err) => {
        this.log(`MTU 设置失败 (将使用默认值): ${err.errMsg}`);
      }
    });

    // 1. 获取所有服务
    await new Promise((resolve, reject) => {
      wx.getBLEDeviceServices({ deviceId, success: resolve, fail: reject });
    });

    // 2. 获取特征值
    await new Promise((resolve, reject) => {
      wx.getBLEDeviceCharacteristics({
        deviceId,
        serviceId,
        success: resolve,
        fail: reject
      });
    });

    // 3. 开启订阅
    await new Promise((resolve, reject) => {
      wx.notifyBLECharacteristicValueChange({
        deviceId,
        serviceId,
        characteristicId: readCharId,
        state: true,
        success: resolve,
        fail: reject
      });
    });

    wx.onBLECharacteristicValueChange((res) => {
      const bytes = new Uint8Array(res.value);
      // Skip frequent telemetry logging (0x90)
      if (bytes.length > 1 && bytes[1] !== 0x90) {
        const hex = Array.from(bytes).map(b => b.toString(16).padStart(2, '0')).join(' ');
        console.log('[BLE Recv]', hex);
      }

      if (this.onDataCallback) {
        this.onDataCallback(res.value);
      }
    });

    this.connected = true;
    this.log('通讯通道已就绪');
    
    // 由于现在使用的是硬件级 Just Works 配对，应用层无需额外认证
    // 直接触发认证成功回调
    if (this.onAuthCallback) {
      this.onAuthCallback();
    }

  }

  send(buffer) {
    if (!this.connected) {
      this.log('[BLE Send] Failed: not connected');
      return;
    }
    const serviceId = '6E400001-B5A3-F393-E0A9-E50E24DCCA9E';
    const writeCharId = '6E400002-B5A3-F393-E0A9-E50E24DCCA9E';

    const bytes = new Uint8Array(buffer);
    const hex = Array.from(bytes).map(b => b.toString(16).padStart(2, '0')).join(' ');
    console.log(`[BLE Send] ${hex}`);

    wx.writeBLECharacteristicValue({
      deviceId: this.deviceId,
      serviceId,
      characteristicId: writeCharId,
      value: buffer,
      writeType: 'writeNoResponse',
      success: () => {
        // console.log('[BLE Send] Success');
      },
      fail: (err) => {
        if (err.errCode === 10006) {
          this.connected = false;
        }
        this.log(`[BLE Send] Failed: ${JSON.stringify(err)}`);
        console.error('[BLE Send] Error:', err);
      }
    });
  }

  disconnect() {
    this.expectDisconnect = true; // 标记为主动断开
    if (this.deviceId) {
      wx.closeBLEConnection({ deviceId: this.deviceId });
    }
    this.connected = false;
  }

  handleAutoReconnect(attempts = 0) {
    if (attempts >= 2) {
      wx.hideLoading();
      wx.showModal({
        title: '连接已断开',
        content: '尝试自动重连失败，请手动重新连接',
        showCancel: false,
        success: () => {
          wx.reLaunch({ url: '/pages/index/index' });
        }
      });
      return;
    }

    wx.showLoading({ title: `正在重连 (${attempts + 1}/2)...`, mask: true });
    
    // 尝试重连
    this.connect(this.deviceId).then(() => {
      wx.hideLoading();
      wx.showToast({ title: '重连成功', icon: 'success' });
    }).catch(() => {
      // 延迟重试，避免请求过快导致失败
      setTimeout(() => {
        this.handleAutoReconnect(attempts + 1);
      }, 1500);
    });
  }
}

export const bleManager = new BLEManager();
