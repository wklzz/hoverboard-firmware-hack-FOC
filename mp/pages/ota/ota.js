import { bleManager } from '../../utils/ble';
import { CmdId, buildFrame, validateFrame, ACK_MASK } from '../../utils/protocol';

const CHUNK_SIZE = 480; // Each chunk size in bytes

Page({
  data: {
    progress: 0,
    speed: 0,
    fileName: '',
    fileSize: 0,
    updating: false,
    fileReady: false,
    logs: [],
    scrollTop: 0
  },

  onLoad() {
    this.binData = null;
    this.addLog('就绪，等待选择固件文件...');
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
          fileSize: file.size,
          fileReady: true
        });
        this.readFile(file.path);
      }
    });
  },

  readFile(path) {
    const fs = wx.getFileSystemManager();
    try {
      this.binData = fs.readFileSync(path);
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
