/**
 * BLE Manager for Hoverboard
 */

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
      if (this.onDataCallback) {
        this.onDataCallback(res.value);
      }
    });
  }

  send(buffer) {
    if (!this.connected) return;
    const serviceId = '6E400001-B5A3-F393-E0A9-E50E24DCCA9E';
    const writeCharId = '6E400002-B5A3-F393-E0A9-E50E24DCCA9E';

    wx.writeBLECharacteristicValue({
      deviceId: this.deviceId,
      serviceId,
      characteristicId: writeCharId,
      value: buffer
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
