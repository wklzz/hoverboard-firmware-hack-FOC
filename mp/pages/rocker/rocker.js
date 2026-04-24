import { bleManager } from '../../utils/ble';
import { CmdId, buildFrame, parseTelemetry, validateFrame, ACK_MASK } from '../../utils/protocol';

Page({
  data: {
    connected: false,
    mode: 2,
    telemetry: {},
    speed: 0,
    running: false,
    currentPhase: 'stopped', // 'forward', 'backward', 'stopped'
    forwardTime: 1.0,
    backwardTime: 1.0,
    powerLimit: 15, // 百分比
    intervalId: null,
    beepEnabled: true,
  },

  onLoad() {
    this.initBleDataHandler();
  },

  onShow() {
    this.setData({ connected: bleManager.connected });
    if (bleManager.connected) {
      this.queryStatus();
    }
  },

  onUnload() {
    this.stopRocker();
    bleManager.onDataCallback = null;
    bleManager.onAuthCallback = null;
  },

  queryStatus() {
    if (bleManager.connected) {
      const frame = buildFrame(CmdId.STATUS, new Uint8Array(0));
      bleManager.send(frame);
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
          const mode = frame[5];
          const beepEnabled = frame[6] !== 0;
          this.setData({ mode, beepEnabled });
          // 自动路由
          if (mode === 0) {
            wx.redirectTo({ url: '/pages/joystick/joystick' });
          } else if (mode === 1) {
            wx.redirectTo({ url: '/pages/dashboard/dashboard' });
          }
        } else if (cmdId === (CmdId.CONFIG | ACK_MASK)) {
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

  toggleMode() {
    const newMode = (this.data.mode + 1) % 3;
    const payload = new Uint8Array([0x01, newMode]);
    const frame = buildFrame(CmdId.CONFIG, payload);
    bleManager.send(frame);
    this.setData({ mode: newMode });
  },

  toggleRocker() {
    if (this.data.running) {
      this.stopRocker();
    } else {
      this.startRocker();
    }
  },

  startRocker() {
    if (this.data.running) return;

    this.setData({ running: true, currentPhase: 'forward' });
    
    let phaseStartTime = Date.now();
    
    const intervalId = setInterval(() => {
      if (!bleManager.connected) {
        this.stopRocker();
        return;
      }

      const { currentPhase, forwardTime, backwardTime, powerLimit } = this.data;
      const elapsed = (Date.now() - phaseStartTime) / 1000;

      const RAMP_TIME = 1.0; // 缓启动/缓停时间
      const holdTime = currentPhase === 'forward' ? forwardTime : backwardTime;
      const totalPhaseTime = RAMP_TIME + holdTime + RAMP_TIME;
      const targetThrottle = currentPhase === 'forward' ? powerLimit * 10 : -powerLimit * 10;

      let throttle = 0;
      let nextPhase = currentPhase;

      if (elapsed < RAMP_TIME) {
        // 缓启动
        throttle = Math.round(targetThrottle * (elapsed / RAMP_TIME));
      } else if (elapsed < RAMP_TIME + holdTime) {
        // 保持
        throttle = targetThrottle;
      } else if (elapsed < totalPhaseTime) {
        // 缓停
        const rampDownElapsed = elapsed - (RAMP_TIME + holdTime);
        throttle = Math.round(targetThrottle * (1 - (rampDownElapsed / RAMP_TIME)));
      } else {
        // 切换阶段
        nextPhase = currentPhase === 'forward' ? 'backward' : 'forward';
        phaseStartTime = Date.now();
        throttle = 0;
      }

      // 发送 DRIVE 指令
      const payload = new Uint8Array(4);
      const view = new DataView(payload.buffer);
      view.setInt16(0, 0, true); // steer = 0
      view.setInt16(2, throttle, true);
      const frame = buildFrame(CmdId.DRIVE, payload);
      bleManager.send(frame);

      if (nextPhase !== currentPhase) {
        this.setData({ currentPhase: nextPhase });
      }
    }, 50);

    this.setData({ intervalId });
    wx.setKeepScreenOn({ keepScreenOn: true });
  },

  stopRocker() {
    if (this.data.intervalId) {
      clearInterval(this.data.intervalId);
    }
    
    this.setData({ 
      running: false, 
      currentPhase: 'stopped', 
      intervalId: null 
    });

    // 发送 5 帧归零包确保停止
    let stopCount = 0;
    const stopTimer = setInterval(() => {
      const payload = new Uint8Array([0, 0, 0, 0]);
      bleManager.send(buildFrame(CmdId.DRIVE, payload));
      stopCount++;
      if (stopCount >= 5) clearInterval(stopTimer);
    }, 50);

    wx.setKeepScreenOn({ keepScreenOn: false });
  },

  toggleBeep(e) {
    const enabled = e.detail.value;
    console.log(`[Rocker] Toggle Beep: ${enabled}`);
    const payload = new Uint8Array([2, enabled ? 1 : 0]);
    const frame = buildFrame(CmdId.CONFIG, payload);
    bleManager.send(frame);
    this.setData({ beepEnabled: enabled });
  },

  adjustParam(e) {
    const { key, val } = e.currentTarget.dataset;
    let newValue = parseFloat((this.data[key] + parseFloat(val)).toFixed(1));
    if (newValue < 0.1) newValue = 0.1;
    if (newValue > 5.0) newValue = 5.0;
    this.setData({ [key]: newValue });
  },

  powerChange(e) {
    this.setData({ powerLimit: e.detail.value });
  },

  disconnect() {
    bleManager.disconnect();
    wx.reLaunch({ url: '/pages/index/index' });
  }
});
