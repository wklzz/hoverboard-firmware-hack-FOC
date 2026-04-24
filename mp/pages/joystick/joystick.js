import { bleManager } from '../../utils/ble';
import { CmdId, buildFrame, parseTelemetry, validateFrame, ACK_MASK } from '../../utils/protocol';

Page({
  data: {
    telemetry: {},
    speed: 0,
    controlMode: 'fixed', // 'fixed' or 'floating'
    touching: false,
    visualX: 0,
    visualY: 0,
    stickX: 0,
    stickY: 0,
    steer: 0,
    throttle: 0,
    speedLimit: 100,
    connected: false,
    mode: 0,
    intervalId: null,
    maxRadius: 150, // Default in px, will be updated
  },

  onLoad() {
    this.rect = null;
    this.loadSettings();
    this.initJoystick();
    this.initBleDataHandler();
    // 移除 onLoad 里的自动启动，改为按需启动
    
    bleManager.onAuthCallback = () => {
      this.setData({ connected: true });
      this.queryStatus();
    };
  },

  onShow() {
    this.setData({ connected: bleManager.connected });
    if (bleManager.connected) {
      this.queryStatus(); // 进入页面时同步模式
    }
  },

  onUnload() {
    this.stopSendingCommands();
    bleManager.onDataCallback = null;
    bleManager.onAuthCallback = null;
  },

  loadSettings() {
    const speedLimit = wx.getStorageSync('speedLimit') || 100;
    const controlMode = wx.getStorageSync('controlMode') || 'fixed';
    this.setData({ speedLimit, controlMode });
  },

  queryStatus() {
    if (bleManager.connected) {
      const frame = buildFrame(CmdId.STATUS, new Uint8Array(0));
      bleManager.send(frame);
    }
  },

  toggleMode() {
    const newMode = (this.data.mode + 1) % 3;
    const payload = new Uint8Array([0x01, newMode]);
    const frame = buildFrame(CmdId.CONFIG, payload);
    bleManager.send(frame);
    // 注意：不立即 setData，等 ACK 或下一帧 Telemetry 同步会更准确
    // 但为了响应感，可以先预设，或者依靠 queryStatus
    this.setData({ mode: newMode });
  },

  initJoystick() {
    const query = wx.createSelectorQuery();
    query.select('.joystick-area').boundingClientRect((rect) => {
      if (rect) {
        this.rect = rect;
        // In fixed mode, place it in the center
        this.setData({
          visualX: rect.width / 2,
          visualY: rect.height / 2,
          maxRadius: rect.width / 2 * 0.8 // Allow large range
        });
      }
    }).exec();
  },

  setControlMode(e) {
    const mode = e.currentTarget.dataset.mode;
    this.setData({ controlMode: mode });
    wx.setStorageSync('controlMode', mode);
    if (mode === 'fixed' && this.rect) {
      this.setData({
        visualX: this.rect.width / 2,
        visualY: this.rect.height / 2
      });
    }
  },

  initBleDataHandler() {
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
        
        if (cmdId === CmdId.TELEMETRY) {
          this.handleTelemetry(frame.slice(4, frameLen - 2));
        } else if (cmdId === (CmdId.STATUS | ACK_MASK)) {
          // payload[0] 是系统状态，payload[1] 是 ctrlMode
          const mode = frame[5]; 
          this.setData({ mode });
          // 自动路由
          if (mode === 1) {
            wx.redirectTo({ url: '/pages/dashboard/dashboard' });
          } else if (mode === 2) {
            wx.redirectTo({ url: '/pages/rocker/rocker' });
          }
        } else if (cmdId === (CmdId.CONFIG | ACK_MASK)) {
          // 配置成功的回复，可以再次查询状态
          this.queryStatus();
        }
        
        bytes = bytes.subarray(frameLen);
      }
    };
  },

  handleTelemetry(payload) {
    const data = parseTelemetry(payload);
    if (!data) return;
    this.setData({
      telemetry: data,
      speed: Math.round(Math.abs(data.speedR + data.speedL) / 2)
    });
  },

  touchStart(e) {
    if (!this.rect) return;
    
    // 启动发送定时器
    this.startSendingCommands();

    const touch = e.touches[0];
    const rect = this.rect;
    const touchX = touch.clientX - rect.left;
    const touchY = touch.clientY - rect.top;

    if (this.data.controlMode === 'floating') {
      this.setData({
        touching: true,
        visualX: touchX,
        visualY: touchY,
        stickX: 0,
        stickY: 0
      });
    } else {
      this.setData({ touching: true });
      this.handleTouch(e);
    }
  },

  touchMove(e) {
    this.handleTouch(e);
  },

  touchEnd() {
    this.setData({
      touching: false,
      stickX: 0,
      stickY: 0,
      steer: 0,
      throttle: 0
    });
    // 注意：这里不需要手动 clearInterval，
    // 定时器会在补发完 10 次归零指令后自动关闭
  },

  handleTouch(e) {
    if (!this.rect || !this.data.touching) return;
    const touch = e.touches[0];
    const rect = this.rect;
    
    // Relative to the visual center
    let dx = (touch.clientX - rect.left) - this.data.visualX;
    let dy = (touch.clientY - rect.top) - this.data.visualY;

    const distance = Math.sqrt(dx * dx + dy * dy);
    const maxRadius = this.data.maxRadius;

    if (distance > maxRadius) {
      dx = (dx / distance) * maxRadius;
      dy = (dy / distance) * maxRadius;
    }

    // Normalize -1000 to 1000
    let steer = Math.round((dx / maxRadius) * 1000);
    let throttle = Math.round(-(dy / maxRadius) * 1000);

    // Dead Zone (5%)
    if (Math.abs(steer) < 50) steer = 0;
    if (Math.abs(throttle) < 50) throttle = 0;

    // Speed Limit
    const limit = this.data.speedLimit / 100;
    steer = Math.round(steer * limit);
    throttle = Math.round(throttle * limit);

    this.setData({
      stickX: dx,
      stickY: dy,
      steer,
      throttle
    });
  },

  startSendingCommands() {
    if (this.data.intervalId) return; // 已经在运行了

    let stopCounter = 0; // 离手后的补发计数器

    const intervalId = setInterval(() => {
      if (!bleManager.connected) {
        this.stopSendingCommands();
        return;
      }

      const { steer, throttle, touching } = this.data;

      // 1. 发送数据
      const payload = new Uint8Array(4);
      const view = new DataView(payload.buffer);
      view.setInt16(0, steer, true);
      view.setInt16(2, throttle, true);
      const frame = buildFrame(CmdId.DRIVE, payload);
      bleManager.send(frame);

      // 2. 状态检查：如果手指离开了
      if (!touching) {
        stopCounter++;
        if (stopCounter >= 10) { // 补发 10 帧归零包后停止
          this.stopSendingCommands();
          console.log('[Joystick] 静默模式：已补发 10 帧归零包，停止发送');
        }
      } else {
        stopCounter = 0; // 只要还在摸，就重置计数器
      }
    }, 50);

    this.setData({ intervalId });
  },

  stopSendingCommands() {
    if (this.data.intervalId) {
      clearInterval(this.data.intervalId);
      this.setData({ intervalId: null });
    }
  },

  stopAll() {
    this.setData({ stickX: 0, stickY: 0, steer: 0, throttle: 0 });
    bleManager.send(buildFrame(CmdId.DRIVE, new Uint8Array([0, 0, 0, 0])));
  },

  goBack() {
    wx.navigateBack();
  },

  disconnect() {
    bleManager.disconnect();
    wx.reLaunch({ url: '/pages/index/index' });
  },

  setTier(e) {
    const val = parseInt(e.currentTarget.dataset.val);
    this.setData({ speedLimit: val });
    wx.setStorageSync('speedLimit', val);
  }
});
