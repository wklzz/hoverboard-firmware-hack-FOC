import { bleManager } from '../../utils/ble';
import { CmdId, buildFrame, parseTelemetry } from '../../utils/protocol';

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
      // 简单字节包拼装逻辑（针对小报文）
      const bytes = new Uint8Array(buffer);
      if (bytes[0] === 0x7E && bytes[1] === 0x90) { // Telemetry
        this.handleTelemetry(buffer.slice(4, buffer.byteLength - 2));
      } else if (bytes[1] === (CmdId.STATUS | 0x80)) { // Status ACK
        const view = new DataView(buffer);
        const mode = view.getUint8(5); // [7E][91][LEN][MODE]...
        this.setData({ mode });
      }
    };
  },

  handleTelemetry(payload) {
    const data = parseTelemetry(payload);
    if (!data) return;

    // 计算 UI 特性
    const speed = Math.abs(data.speedR + data.speedL) / 2;
    const speedDeg = (speed / 1000) * 180 - 90;
    
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
    const newMode = this.data.mode === 0 ? 1 : 0;
    // 发送 CONFIG (0x20), key=1, val=newMode
    const payload = new Uint8Array([1, newMode]);
    const frame = buildFrame(CmdId.CONFIG, payload);
    bleManager.send(frame);
    
    // 乐观更新
    this.setData({ mode: newMode });
    wx.showToast({ title: `切换至${newMode === 0 ? '手控' : '遥控'}任务`, icon: 'none' });
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
