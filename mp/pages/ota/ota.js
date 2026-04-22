import { bleManager } from '../../utils/ble';
import { CmdId, buildFrame, validateFrame, ACK_MASK, compareVersions } from '../../utils/protocol';

const CHUNK_SIZE = 200; // Each chunk size in bytes (Reduced for compatibility with older firmware)
const BASE_URL = 'http://wcart.wozer.cn/api';

Page({
  data: {
    progress: 0,
    speed: 0,
    fileName: '',
    fileSize: 0,
    updating: false,
    fileReady: false,
    logs: [],
    scrollTop: 0,
    deviceVersion: '未知'
  },

  onLoad() {
    this.binData = null;
    this.addLog('就绪，等待选择固件文件...');
    if (bleManager.connected) {
      this.queryDeviceVersion();
    }
  },

  queryDeviceVersion() {
    this.addLog('正在读取设备版本...');
    
    const originalCallback = bleManager.onDataCallback;
    bleManager.onDataCallback = (buffer) => {
      let bytes = new Uint8Array(buffer);
      while (bytes.length >= 6) {
        if (!validateFrame(bytes)) {
          bytes = bytes.subarray(1);
          continue;
        }
        const cmdId = bytes[1];
        const plen = bytes[2] | (bytes[3] << 8);
        if (cmdId === (CmdId.STATUS | ACK_MASK)) {
          // Payload: [State:1][Mode:1][Version:N]
          if (plen > 2) {
            const versionArr = bytes.subarray(6, 4 + plen);
            let versionStr = "";
            for(let i=0; i<versionArr.length; i++) versionStr += String.fromCharCode(versionArr[i]);
            this.setData({ deviceVersion: versionStr });
            this.addLog(`设备版本: ${versionStr}`);
          }
          // 恢复原回调或设为 null
          bleManager.onDataCallback = originalCallback;
          return;
        }
        bytes = bytes.subarray(6 + plen);
      }
    };
    
    bleManager.send(buildFrame(CmdId.STATUS));
  },

  addLog(msg) {
    const logs = this.data.logs;
    const time = new Date().toLocaleTimeString('en-GB', { hour12: false });
    logs.push({ time, msg });
    this.setData({ logs, scrollTop: logs.length * 100 });
  },

  chooseFile() {
    wx.chooseMessageFile({
      count: 1,
      type: 'file',
      extension: ['bin'],
      success: (res) => {
        const file = res.tempFiles[0];
        this.setData({
          fileName: file.name,
          fileSize: (file.size / 1024).toFixed(1),
          fileReady: true
        });
        this.readFile(file.path);
      }
    });
  },

  async checkCloudUpdate() {
    if (!bleManager.connected) {
      wx.showToast({ title: '蓝牙未连接', icon: 'none' });
      return;
    }

    this.addLog('正在检查云端版本...');
    
    wx.request({
      url: `${BASE_URL}/ota/check`,
      data: {
        device_id: bleManager.deviceId,
        version: this.data.deviceVersion,
        target: 'esp32'
      },
      success: (res) => {
        if (res.data && res.data.version) {
          const update = res.data;
          const cmp = compareVersions(update.version, this.data.deviceVersion);
          
          if (cmp > 0) {
            this.addLog(`发现新版本: ${update.version}`);
            wx.showModal({
              title: '发现新版本',
              content: `当前版本: ${this.data.deviceVersion}\n最新版本: ${update.version}\n描述: ${update.description || '无'}\n是否下载并升级？`,
              success: (modalRes) => {
                if (modalRes.confirm) {
                  this.downloadCloudFirmware(update.url, update.version);
                }
              }
            });
          } else {
            this.addLog(`当前版本 ${this.data.deviceVersion} 已是最新`);
            wx.showToast({ title: '已是最新版本', icon: 'success' });
          }
        } else {
          this.addLog('服务器暂无更新信息');
        }
      },
      fail: () => {
        this.addLog('检查失败，请确认网络连接');
      }
    });
  },

  downloadCloudFirmware(url, version) {
    this.addLog(`正在下载固件 v${version}...`);
    // 如果返回的是相对路径，补全域名
    const downloadUrl = url.startsWith('http') ? url : `${BASE_URL.replace('/api', '')}${url}`;
    
    wx.downloadFile({
      url: downloadUrl,
      success: (res) => {
        if (res.statusCode === 200) {
          this.setData({
            fileName: `OTA_v${version}.bin`,
            fileSize: (res.tempFilePath.length / 1024).toFixed(1), // Note: tempFilePath length is not accurate for size
            fileReady: true
          });
          this.readFile(res.tempFilePath);
          this.addLog('云端固件下载完成，点击开始升级');
        } else {
          this.addLog(`下载失败: 状态码 ${res.statusCode}`);
        }
      },
      fail: (e) => {
        this.addLog(`下载出错: ${e.errMsg}`);
      }
    });
  },

  readFile(path) {
    const fs = wx.getFileSystemManager();
    try {
      this.binData = fs.readFileSync(path);
      this.setData({
        fileSize: (this.binData.byteLength / 1024).toFixed(1)
      });
      this.addLog(`文件已加载: ${this.binData.byteLength} 字节`);
    } catch (e) {
      this.addLog(`读取失败: ${e.message}`);
    }
  },

  async startUpdate() {
    if (!this.binData || !bleManager.connected) return;

    this.setData({ updating: true, progress: 0 });
    this.addLog('开始更新流程...');

    // 1. 发送 OTA_BEGIN
    const payload = new Uint8Array(5);
    const view = new DataView(payload.buffer);
    view.setUint32(0, this.binData.byteLength, true);
    payload[4] = 0; // ESP32 OTA
    
    this.addLog('正在请求 ESP32 进入 OTA 模式...');
    const frame = buildFrame(CmdId.OTA_BEGIN, payload);
    bleManager.send(frame);

    // 设置监听
    this.setupOtaListener();
  },

  setupOtaListener() {
    let offset = 0;
    let startTime = Date.now();

    bleManager.onDataCallback = (buffer) => {
      let bytes = new Uint8Array(buffer);
      while (bytes.length >= 6) {
        if (!validateFrame(bytes)) {
          bytes = bytes.subarray(1);
          continue;
        }
        const cmdId = bytes[1];
        const plen = bytes[2] | (bytes[3] << 8);
        const frameLen = 6 + plen;
        const frame = bytes.subarray(0, frameLen);

        if (cmdId === (CmdId.OTA_BEGIN | ACK_MASK)) {
          if (plen === 0) {
            this.addLog('ESP32 已就绪，开始传输数据...');
            this.sendNextChunk(0);
          } else {
            this.addLog('ESP32 拒绝升级 (Error 0x' + frame[4].toString(16) + ')');
            this.stopUpdating();
          }
        } else if (cmdId === (CmdId.OTA_DATA | ACK_MASK)) {
          const received = (frame[4]) | (frame[5] << 8) | (frame[6] << 16) | (frame[7] << 24);
          this.handleChunkAck(received, startTime);
        } else if (cmdId === (CmdId.OTA_END | ACK_MASK)) {
          this.addLog('升级成功！设备正在重启...');
          this.setData({ progress: 100 });
          setTimeout(() => {
            wx.reLaunch({ url: '/pages/index/index' });
          }, 2000);
        }

        bytes = bytes.subarray(frameLen);
      }
    };
  },

  sendNextChunk(offset) {
    if (offset >= this.binData.byteLength) {
      this.addLog('传输完成，等待最终校验...');
      const frame = buildFrame(CmdId.OTA_END);
      bleManager.send(frame);
      return;
    }

    const end = Math.min(offset + CHUNK_SIZE, this.binData.byteLength);
    const chunk = this.binData.slice(offset, end);
    const payload = new Uint8Array(4 + chunk.byteLength);
    const view = new DataView(payload.buffer);
    view.setUint32(0, offset, true);
    payload.set(new Uint8Array(chunk), 4);

    const frame = buildFrame(CmdId.OTA_DATA, payload);
    bleManager.send(frame);
  },

  handleChunkAck(received, startTime) {
    const progress = Math.round((received / this.binData.byteLength) * 100);
    const elapsed = (Date.now() - startTime) / 1000;
    const speed = (received / 1024 / elapsed).toFixed(1);
    
    this.setData({ progress, speed });
    this.sendNextChunk(received);
  },

  stopUpdating() {
    this.setData({ updating: false });
    bleManager.onDataCallback = null;
  },

  abortUpdate() {
    this.addLog('更新被用户中止');
    this.stopUpdating();
  }
});
