import { bleManager } from '../../utils/ble';
import { CmdId, buildFrame, parseTelemetry, validateFrame, ACK_MASK } from '../../utils/protocol';
import { otaHelper } from '../../utils/ota';

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
    beepEnabled: true,
  },

  onLoad() {
    this.initBleDataHandler();
    bleManager.onAuthCallback = () => {
      this.setData({ connected: true });
      this.requestStatus();
    };
  },

  onShow() {
    this.setData({ connected: bleManager.connected });
    if (bleManager.connected) {
      this.requestStatus();
    }
  },

  onUnload() {
    bleManager.onDataCallback = null;
    bleManager.onAuthCallback = null;
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
        } else if (cmdId === (CmdId.STATUS | ACK_MASK)) {
          const view = new DataView(frame.buffer, frame.byteOffset, frameLen);
          const state = view.getUint8(4);
          const mode = view.getUint8(5);
          const beepEnabled = view.getUint8(6) !== 0;
          console.log(`[Dashboard] STATUS ACK received - State: ${state}, Mode: ${mode}, Beep: ${beepEnabled}`);
          this.setData({ mode, beepEnabled });

          // 解析版本号 (新结构)
          if (plen >= 4) {
            const espVerLen = bytes[7];
            const espVerArr = bytes.subarray(8, 8 + espVerLen);
            let espVersion = "";
            for(let i=0; i<espVerArr.length; i++) espVersion += String.fromCharCode(espVerArr[i]);
            
            // STM32 版本 (小端 uint16)
            const stmVerPos = 8 + espVerLen;
            let stmVersion = "未知";
            if (plen >= (espVerLen + 6)) {
              const vRaw = bytes[stmVerPos]; // 8-bit version
              if (vRaw > 0) {
                // 将 10 转为 v1.0
                const major = Math.floor(vRaw / 10);
                const minor = vRaw % 10;
                stmVersion = `v${major}.${minor}`;
              }
            }
            
            console.log(`[Dashboard] ESP: ${espVersion}, STM: ${stmVersion}`);
            this.setData({ espVersion, stmVersion });
            
            // 自动检查 OTA (传入解析后的版本对象)
            this.checkingOta = true;
            otaHelper.checkAndPrompt({
              esp: espVersion,
              stm: stmVersion
            }).then(() => {
              this.checkingOta = false;
            });
          }
          
          // 自动路由 (如果正在检查 OTA，则暂不跳转)
          if (this.checkingOta) return;

          if (mode === 0) {
            wx.redirectTo({ url: '/pages/joystick/joystick' });
          } else if (mode === 2) {
            wx.redirectTo({ url: '/pages/rocker/rocker' });
          }
        } else if (cmdId === (CmdId.CONFIG | ACK_MASK)) {
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

    const newMode = (this.data.mode + 1) % 3;
    console.log(`[Dashboard] Toggle Mode triggered, setting mode to: ${newMode}`);
    // 发送 CONFIG (0x20), key=1, val=newMode
    const payload = new Uint8Array([1, newMode]);
    const frame = buildFrame(CmdId.CONFIG, payload);
    bleManager.send(frame);
    
    // 乐观更新
    this.setData({ mode: newMode });
    const modeNames = ['蓝牙手控', '遥控模式', '摇摇车'];
    wx.showToast({ title: `模式: ${modeNames[newMode]}`, icon: 'none' });
  },

  toggleBeep(e) {
    const enabled = e.detail.value;
    console.log(`[Dashboard] Toggle Beep: ${enabled}`);
    const payload = new Uint8Array([2, enabled ? 1 : 0]);
    const frame = buildFrame(CmdId.CONFIG, payload);
    bleManager.send(frame);
    this.setData({ beepEnabled: enabled });
  },

  requestStatus() {
    if (bleManager.connected) {
      const frame = buildFrame(CmdId.STATUS);
      bleManager.send(frame);
    }
  },

  disconnect() {
    bleManager.disconnect();
    wx.reLaunch({ url: '/pages/index/index' });
  }
})
