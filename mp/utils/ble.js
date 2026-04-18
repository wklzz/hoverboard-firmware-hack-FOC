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
  }

  log(msg) {
    console.log('[BLE]', msg);
    if (this.onStatusCallback) this.onStatusCallback(msg);
  }

  init() {
    return new Promise((resolve, reject) => {
      wx.onBLEConnectionStateChange((res) => {
        this.log(`连接状态变化: ${res.connected ? '已连接' : '已断开'}`);
        this.connected = res.connected;
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

    this.connected = true;
    this.log('通讯通道已就绪');

    wx.onBLECharacteristicValueChange((res) => {
      const bytes = new Uint8Array(res.value);
      // Skip frequent telemetry logging (0x90)
      if (bytes.length > 1 && bytes[1] !== 0x90) {
        const hex = Array.from(bytes).map(b => b.toString(16).padStart(2, '0')).join(' ');
        console.log('[BLE Recv]', hex);
      }

      // Authentication Handler
      if (bytes.length >= 6 && bytes[0] === 0x7E) {
        if (bytes[1] === CmdId.AUTH_REQ) { // Received Challenge
          const view = new DataView(res.value);
          if (res.value.byteLength >= 8) {
            const challenge = view.getUint32(4, true); // Little endian
            console.log(`[BLE Auth] Received Challenge: 0x${challenge.toString(16)}`);
            
            const response = (challenge ^ 0x12345678) >>> 0;
            const payload = new Uint8Array(4);
            const payloadView = new DataView(payload.buffer);
            payloadView.setUint32(0, response, true);
            
            const frame = buildFrame(CmdId.AUTH_RES, payload);
            this.send(frame);
            return;
          }
        } else if (bytes[1] === (CmdId.AUTH_RES | 0x80)) { // AUTH_RES ACK
          console.log('[BLE Auth] Authenticated Successfully!');
          wx.showToast({ title: '设备已认证', icon: 'success' });
          return;
        }
      }

      if (this.onDataCallback) {
        this.onDataCallback(res.value);
      }
    });

    // 4. 发起认证请求
    const authReqFrame = buildFrame(CmdId.AUTH_REQ);
    this.send(authReqFrame);
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
    if (this.deviceId) {
      wx.closeBLEConnection({ deviceId: this.deviceId });
    }
    this.connected = false;
  }
}

export const bleManager = new BLEManager();
