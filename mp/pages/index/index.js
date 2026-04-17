import { bleManager } from '../../utils/ble';

Page({
  data: {
    scanning: false,
    devices: [],
  },

  onLoad() {
    bleManager.onStatusCallback = (msg) => {
      wx.showToast({ title: msg, icon: 'none' });
    };
  },

  onHide() {
    this.stopScan();
  },

  toggleScan() {
    if (this.data.scanning) {
      this.stopScan();
    } else {
      this.startScan();
    }
  },

  async startScan() {
    try {
      await bleManager.init();
      this.setData({ scanning: true, devices: [] });
      bleManager.startScan((device) => {
        const devices = this.data.devices;
        if (!devices.find(d => d.deviceId === device.deviceId)) {
          devices.push(device);
          // 排序：信号强的在前
          devices.sort((a, b) => b.RSSI - a.RSSI);
          this.setData({ devices });
        }
      });
    } catch (err) {
      console.error(err);
    }
  },

  stopScan() {
    bleManager.stopScan();
    this.setData({ scanning: false });
  },

  connectDevice(e) {
    const deviceId = e.currentTarget.dataset.id;
    this.stopScan();
    
    wx.showLoading({ title: '正在连接...' });
    bleManager.connect(deviceId).then(() => {
      wx.hideLoading();
      wx.navigateTo({
        url: '/pages/dashboard/dashboard',
      });
    }).catch(err => {
      wx.hideLoading();
      wx.showModal({
        title: '连接失败',
        content: err.errMsg || '无法建立连接',
        showCancel: false
      });
    });
  }
})
