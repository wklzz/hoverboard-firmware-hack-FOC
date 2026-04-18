import { bleManager } from '../../utils/ble';
import { CmdId, buildFrame, parseTelemetry, validateFrame } from '../../utils/protocol';

Page({
  data: {
    telemetry: {},
    speed: 0,
    stickX: 0,
    stickY: 0,
    steer: 0,
    throttle: 0,
    speedLimit: 100, // Speed percentage (10-100)
    intervalId: null,
    centerX: 0,
    centerY: 0,
    maxRadius: 110, // Approximate max radius in pixels (360rpx / 2 - stickRadius)
  },

  onLoad() {
    this.rect = null; // Cache for performance
    this.initJoystick();
    this.initBleDataHandler();
    this.startSendingCommands();
  },

  onUnload() {
    this.stopSendingCommands();
  },

  initJoystick() {
    const query = wx.createSelectorQuery();
    query.select('.joystick-container').boundingClientRect((rect) => {
      if (rect) {
        this.rect = rect;
        this.setData({
          centerX: rect.width / 2,
          centerY: rect.height / 2,
          maxRadius: (rect.width * 0.9) / 2 - 35 // Account for stick size
        });
      }
    }).exec();
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
    const speed = Math.round(Math.abs(data.speedR + data.speedL) / 2);
    this.setData({
      telemetry: data,
      speed
    });
  },

  touchStart(e) {
    this.handleTouch(e);
  },

  touchMove(e) {
    this.handleTouch(e);
  },

  touchEnd() {
    this.setData({
      stickX: 0,
      stickY: 0,
      steer: 0,
      throttle: 0
    });
  },

  handleTouch(e) {
    if (!this.rect) return;
    const touch = e.touches[0];
    const rect = this.rect;
    
    let dx = touch.clientX - (rect.left + rect.width / 2);
    let dy = touch.clientY - (rect.top + rect.height / 2);

    const distance = Math.sqrt(dx * dx + dy * dy);
    const maxRadius = this.data.maxRadius;

    if (distance > maxRadius) {
      dx = (dx / distance) * maxRadius;
      dy = (dy / distance) * maxRadius;
    }

    // Calculate normalized values (-1000 to 1000)
    let steer = Math.round((dx / maxRadius) * 1000);
    let throttle = Math.round(-(dy / maxRadius) * 1000); // Inverse Y

    // Apply Dead Zone (5%)
    const deadZone = 50;
    if (Math.abs(steer) < deadZone) steer = 0;
    if (Math.abs(throttle) < deadZone) throttle = 0;

    // Apply Speed Limit
    const limit = (this.data.speedLimit || 100) / 100;
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

      // Build DRIVE payload: [steer:int16][speed:int16] (Little Endian)
      const { steer, throttle } = this.data;
      const payload = new Uint8Array(4);
      const view = new DataView(payload.buffer);
      
      view.setInt16(0, steer, true);
      view.setInt16(2, throttle, true);

      const frame = buildFrame(CmdId.DRIVE, payload);
      bleManager.send(frame);
    }, 50); // 20Hz update rate
    this.setData({ intervalId });
  },

  stopSendingCommands() {
    if (this.data.intervalId) {
      clearInterval(this.data.intervalId);
    }
    // Final zero command
    const payload = new Uint8Array([0, 0, 0, 0]);
    const frame = buildFrame(CmdId.DRIVE, payload);
    bleManager.send(frame);
  },

  stopAll() {
    this.setData({
      stickX: 0,
      stickY: 0,
      steer: 0,
      throttle: 0
    });
    const payload = new Uint8Array([0, 0, 0, 0]);
    const frame = buildFrame(CmdId.DRIVE, payload);
    bleManager.send(frame);
  },

  goBack() {
    wx.navigateBack();
  },

  onSpeedLimitChange(e) {
    this.setData({
      speedLimit: e.detail.value
    });
  }
});
