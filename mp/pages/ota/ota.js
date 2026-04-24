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
    deviceVersion: '未知',
    target: 'esp32'
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
        this.addLog('本地文件选择成功，正在启动升级...');
        setTimeout(() => {
          this.startUpdate();
        }, 500);
      }
    });
  },

  async checkCloudUpdate() {
    if (!bleManager.connected) {
      wx.showToast({ title: '蓝牙未连接', icon: 'none' });
      return;
    }

    this.addLog('正在检查云端版本...');
    
    if (this.data.deviceVersion === '未知') {
      this.addLog('未获取到设备版本，正在重试...');
      this.queryDeviceVersion();
      // 等待一小会儿再继续，或者直接提示用户再次点击
      wx.showToast({ title: '正在获取设备版本，请稍后再试', icon: 'none' });
      return;
    }
    
    wx.request({
      url: `${BASE_URL}/ota/check`,
      data: {
        device_id: bleManager.deviceId,
        version: this.data.deviceVersion,
        target: this.data.target
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
          this.addLog('固件下载完成，正在自动启动升级...');
          // 稍微延迟一下，确保状态更新
          setTimeout(() => {
            this.startUpdate();
          }, 500);
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

  switchTarget(e) {
    const target = e.currentTarget.dataset.target;
    this.setData({ target });
    this.addLog(`切换目标为: ${target.toUpperCase()}`);
  },

  async startUpdate() {
    if (!this.binData || !bleManager.connected) return;

    this.setData({ updating: true, progress: 0 });
    this.addLog('开始更新流程...');
    
    // 保持屏幕常亮
    wx.setKeepScreenOn({ keepScreenOn: true });

    if (this.data.target === 'esp32') {
      this.startEsp32Update();
    } else {
      this.startStm32Update();
    }
  },

  async startEsp32Update() {
    // 1. 发送 OTA_BEGIN
    const payload = new Uint8Array(5);
    const view = new DataView(payload.buffer);
    view.setUint32(0, this.binData.byteLength, true);
    payload[4] = 0; // ESP32 OTA
    
    this.addLog('正在请求 ESP32 进入 OTA 模式...');
    const frame = buildFrame(CmdId.OTA_BEGIN, payload);
    bleManager.send(frame);

    // 设置监听
    this.setupEsp32OtaListener();
  },

  setupEsp32OtaListener() {
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
          // 恢复屏幕亮度设置
          wx.setKeepScreenOn({ keepScreenOn: false });
          setTimeout(() => {
            wx.reLaunch({ url: '/pages/index/index' });
          }, 2000);
        }

        bytes = bytes.subarray(frameLen);
      }
    };
  },

  // --- STM32 OTA 协议流 (PING -> INFO -> ERASE -> WRITE -> BOOT) ---
  async startStm32Update() {
    this.addLog('正在请求 STM32 重启并进入 Bootloader...');
    this.stm32Offset = 0;
    this.stm32StartTime = Date.now();
    this.stm32BaseAddr = 0x08004000; // 默认，随后通过 INFO 确认
    this.stm32MaxSize = 0;

    this.setupStm32OtaListener();
    
    // 第一步：发送 PING 同步
    this.addLog('[STM32] 发送 PING (握手)...');
    bleManager.send(buildFrame(CmdId.PING));
  },

  setupStm32OtaListener() {
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

        if (cmdId === (CmdId.PING | ACK_MASK)) {
          this.addLog('[STM32] 握手成功！获取版本: v' + frame[4] + '.' + frame[5]);
          this.addLog('[STM32] 获取 Flash 信息...');
          bleManager.send(buildFrame(CmdId.INFO));
        } 
        else if (cmdId === (CmdId.INFO | ACK_MASK)) {
          const view = new DataView(frame.buffer, frame.byteOffset + 4, 8);
          this.stm32BaseAddr = view.getUint32(0, true);
          this.stm32MaxSize = view.getUint32(4, true);
          this.addLog(`[STM32] 地址: 0x${this.stm32BaseAddr.toString(16).toUpperCase()}, 最大: ${(this.stm32MaxSize/1024).toFixed(1)}KB`);
          
          if (this.binData.byteLength > this.stm32MaxSize) {
            this.addLog('[-] 固件太大，超过主板容量！');
            this.stopUpdating();
            return;
          }

          this.addLog('[STM32] 正在擦除 Flash...');
          bleManager.send(buildFrame(CmdId.ERASE));
        }
        else if (cmdId === (CmdId.ERASE | ACK_MASK)) {
          if (frame[4] === 0) {
            this.addLog('[STM32] 擦除完成，开始传输数据...');
            this.sendNextStm32Chunk(0);
          } else {
            this.addLog('[-] 擦除失败！');
            this.stopUpdating();
          }
        }
        else if (cmdId === (CmdId.WRITE | ACK_MASK)) {
          if (frame[4] === 0) {
            this.stm32Offset += this.lastChunkSize;
            this.handleChunkAck(this.stm32Offset, this.stm32StartTime);
            this.sendNextStm32Chunk(this.stm32Offset);
          } else {
            const errMap = { 0x10: "写保护", 0x11: "编程错误" };
            this.addLog(`[-] 写入失败: ${errMap[frame[4]] || '错误码 ' + frame[4]}`);
            this.stopUpdating();
          }
        }
        else if (cmdId === (CmdId.BOOT | ACK_MASK)) {
          if (frame[4] === 0) {
            this.addLog('[STM32] 升级成功！主控正在启动应用...');
            this.setData({ progress: 100 });
            wx.setKeepScreenOn({ keepScreenOn: false });
            setTimeout(() => { wx.reLaunch({ url: '/pages/index/index' }); }, 2000);
          } else {
            this.addLog('[-] 跳转失败，应用区可能无效！');
            this.stopUpdating();
          }
        }

        bytes = bytes.subarray(frameLen);
      }
    };
  },

  sendNextStm32Chunk(offset) {
    if (offset >= this.binData.byteLength) {
      this.addLog('[STM32] 传输完成，正在验证并启动...');
      bleManager.send(buildFrame(CmdId.BOOT));
      return;
    }

    const chunk_max = 128; // STM32 bootloader buffer limit
    const end = Math.min(offset + chunk_max, this.binData.byteLength);
    let chunk = this.binData.slice(offset, end);
    this.lastChunkSize = chunk.byteLength;

    // 协议包: [addr:4][len:2][data:N]
    const payload = new Uint8Array(6 + chunk.byteLength);
    const view = new DataView(payload.buffer);
    view.setUint32(0, this.stm32BaseAddr + offset, true);
    view.setUint16(4, chunk.byteLength, true);
    payload.set(new Uint8Array(chunk), 6);

    bleManager.send(buildFrame(CmdId.WRITE, payload));
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
    // 恢复屏幕亮度设置
    wx.setKeepScreenOn({ keepScreenOn: false });
  },

  abortUpdate() {
    this.addLog('更新被用户中止');
    this.stopUpdating();
  }
});
