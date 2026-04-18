import { bleManager } from '../../utils/ble';
import { CmdId, buildFrame, parseTelemetry, validateFrame } from '../../utils/protocol';

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
    intervalId: null,
    maxRadius: 150, // Default in px, will be updated
  },

  onLoad() {
    this.rect = null;
    this.initJoystick();
    this.initBleDataHandler();
    this.startSendingCommands();
  },

  onUnload() {
    this.stopSendingCommands();
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
        if (frame[1] === CmdId.TELEMETRY) {
          this.handleTelemetry(frame.slice(4, frameLen - 2));
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
    const intervalId = setInterval(() => {
      if (!bleManager.connected) return;
      const { steer, throttle } = this.data;
      const payload = new Uint8Array(4);
      const view = new DataView(payload.buffer);
      view.setInt16(0, steer, true);
      view.setInt16(2, throttle, true);
      const frame = buildFrame(CmdId.DRIVE, payload);
      bleManager.send(frame);
    }, 50);
    this.setData({ intervalId });
  },

  stopSendingCommands() {
    if (this.data.intervalId) clearInterval(this.data.intervalId);
    const frame = buildFrame(CmdId.DRIVE, new Uint8Array([0, 0, 0, 0]));
    bleManager.send(frame);
  },

  stopAll() {
    this.setData({ stickX: 0, stickY: 0, steer: 0, throttle: 0 });
    bleManager.send(buildFrame(CmdId.DRIVE, new Uint8Array([0, 0, 0, 0])));
  },

  goBack() {
    wx.navigateBack();
  },

  onSpeedLimitChange(e) {
    this.setData({ speedLimit: e.detail.value });
  }
});
