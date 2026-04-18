import { bleManager } from '../../utils/ble';
import { CmdId, buildFrame, parseTelemetry, validateFrame } from '../../utils/protocol';

Page({
  data: {
    connected: true,
    mode: 0,
    speed: 0,
    speedDeg: -90, // -90 to 90 degrees
    telemetry: {},
    batPercent: 0,
    batColor: '#00ff88',
    tempColor: '#00ff88',
    absCmd1: 0,
    absCmd2: 0,
  },

  onLoad() {
    this.initBleDataHandler();
    // 初始查询状态
    this.sendPing();
    this.requestStatus();
  },

  onUnload() {
    // 页面关闭时不一定断开连接，保持运行
  },

  initBleDataHandler() {
    bleManager.onDataCallback = (buffer) => {
      let bytes = new Uint8Array(buffer);
      
      // Handle potential multiple frames in one buffer
      while (bytes.length >= 6) {
        if (!validateFrame(bytes)) {
          // If not a valid frame, skip one byte and try again to find SOF
          bytes = bytes.subarray(1);
          continue;
        }

        const plen = bytes[2] | (bytes[3] << 8);
        const frameLen = 6 + plen;
        const frame = bytes.subarray(0, frameLen);
        const cmdId = frame[1];

        if (cmdId === CmdId.TELEMETRY) {
          this.handleTelemetry(frame.slice(4, frameLen - 2));
        } else if (cmdId === (CmdId.STATUS | 0x80)) {
          const view = new DataView(frame.buffer, frame.byteOffset, frameLen);
          const state = view.getUint8(4);
          const mode = view.getUint8(5);
          console.log(`[Dashboard] STATUS ACK received - State: ${state}, Mode: ${mode}`);
          this.setData({ mode });
        } else if (cmdId === (CmdId.CONFIG | 0x80)) {
          console.log('[Dashboard] CONFIG ACK received, requesting latest status...');
          this.requestStatus();
        }

        // Move to next frame in buffer
        bytes = bytes.subarray(frameLen);
      }
    };
  },

  handleTelemetry(payload) {
    const data = parseTelemetry(payload);
    if (!data) return;

    // 计算 UI 特性
    const speed = Math.abs(data.speedR + data.speedL) / 2;
    // 映射到 -45 到 135 度 (180度范围，对应 0-1000 RPM)
    const speedDeg = (speed / 1000) * 180 - 45;
    
    // 电池百分比估算 (假设 36V 系统, 30V-42V range)
    const volt = data.batVoltage;
    let batPercent = ((volt - 30) / (42 - 30)) * 100;
    batPercent = Math.max(0, Math.min(100, batPercent));
    
    const batColor = batPercent < 20 ? '#ff4d4d' : (batPercent < 50 ? '#ffaa00' : '#00ff88');
    const tempColor = data.boardTemp > 60 ? '#ff4d4d' : '#00ff88';

    this.setData({
      telemetry: data,
      speed: Math.round(speed),
      speedDeg,
      batPercent: Math.round(batPercent),
      batColor,
      tempColor,
      absCmd1: Math.abs(data.cmd1),
      absCmd2: Math.abs(data.cmd2)
    });
  },

  toggleMode() {
    if (this.switching) return;
    this.switching = true;
    setTimeout(() => { this.switching = false; }, 800);

    const newMode = this.data.mode === 0 ? 1 : 0;
    console.log(`[Dashboard] Toggle Mode triggered, setting mode to: ${newMode}`);
    // 发送 CONFIG (0x20), key=1, val=newMode
    const payload = new Uint8Array([1, newMode]);
    const frame = buildFrame(CmdId.CONFIG, payload);
    bleManager.send(frame);
    
    // 乐观更新
    this.setData({ mode: newMode });
    wx.showToast({ title: `模式: ${newMode === 0 ? '蓝牙手控' : '遥控模式'}`, icon: 'none' });

    // 如果切换到手控模式，自动跳转到摇杆页面
    if (newMode === 0) {
      setTimeout(() => {
        wx.navigateTo({ url: '/pages/joystick/joystick' });
      }, 500);
    }
  },

  enterJoystick() {
    wx.navigateTo({ url: '/pages/joystick/joystick' });
  },

  requestStatus() {
    const frame = buildFrame(CmdId.STATUS);
    bleManager.send(frame);
  },

  sendPing() {
    const frame = buildFrame(CmdId.PING);
    bleManager.send(frame);
  },

  disconnect() {
    bleManager.disconnect();
    wx.navigateBack();
  }
})
