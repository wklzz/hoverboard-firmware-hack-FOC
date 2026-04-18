import { bleManager } from '../../utils/ble';
import { CmdId, buildFrame, validateFrame, ACK_MASK } from '../../utils/protocol';

Page({
  data: {
    scanning: false,
    devices: [],
  },

  onLoad() {
    bleManager.onStatusCallback = (msg) => {
      // 仅展示非调试类的提示
      if (!msg.includes('BLE')) {
        wx.showToast({ title: msg, icon: 'none' });
      }
    };
  },

  onShow() {
    this.startScan(); // 自动扫描
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
    if (this.data.scanning) return;
    try {
      await bleManager.init();
      this.setData({ scanning: true, devices: [] });
      bleManager.startScan((device) => {
        const devices = this.data.devices;
        if (!devices.find(d => d.deviceId === device.deviceId)) {
          devices.push(device);
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
    
    wx.showLoading({ title: '正在连接...', mask: true });

    // 1. 设置认证成功后的逻辑：查询状态
    bleManager.onAuthCallback = () => {
      wx.showLoading({ title: '同步模式...', mask: true });
      const frame = buildFrame(CmdId.STATUS);
      bleManager.send(frame);
    };

    // 2. 监听回包以执行重定向
    bleManager.onDataCallback = (buffer) => {
      let bytes = new Uint8Array(buffer);
      while (bytes.length >= 6) {
        if (!validateFrame(bytes)) {
          bytes = bytes.subarray(1);
          continue;
        }
        const plen = bytes[2] | (bytes[3] << 8);
        const frameLen = 6 + plen;
        const frame = bytes.subarray(0, frameLen);
        const cmdId = frame[1];

        if (cmdId === (CmdId.STATUS | ACK_MASK)) {
          wx.hideLoading();
          const mode = frame[5]; // payload[1]
          console.log(`[Index] Mode detected: ${mode}`);
          
          // 清理回调防止重复触发
          bleManager.onAuthCallback = null;
          bleManager.onDataCallback = null;

          if (mode === 0) {
            wx.redirectTo({ url: '/pages/joystick/joystick' });
          } else {
            wx.redirectTo({ url: '/pages/dashboard/dashboard' });
          }
        }
        bytes = bytes.subarray(frameLen);
      }
    };

    bleManager.connect(deviceId).catch(err => {
      wx.hideLoading();
      bleManager.onAuthCallback = null;
      bleManager.onDataCallback = null;
      wx.showModal({
        title: '连接失败',
        content: err.errMsg || '无法建立连接',
        showCancel: false
      });
    });
  }
})
