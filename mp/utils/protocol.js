/**
 * Protocol C & A Parser for Mini Program
 * Based on protocol.h of Hoverboard-FOC-ESP32
 */

export const PKT_SOF = 0x7E;
export const ACK_MASK = 0x80;

export const CmdId = {
  PING: 0x01,
  INFO: 0x02,
  ERASE: 0x03,
  WRITE: 0x04,
  BOOT: 0x05,
  DRIVE: 0x10,
  STATUS: 0x11,
  CONFIG: 0x20,
  AUTH_REQ: 0x30,
  AUTH_RES: 0x31,
  TELEMETRY: 0x90,
  OTA_BEGIN: 0x40,
  OTA_DATA: 0x41,
  OTA_END: 0x42
};

/**
 * CRC16-CCITT Algorithm (matching ESP32/STM32)
 * Initial: 0xFFFF, Poly: 0x1021
 */
export function crc16_ccitt(buf) {
  let crc = 0xFFFF;
  for (let i = 0; i < buf.length; i++) {
    crc ^= (buf[i] << 8);
    for (let j = 0; j < 8; j++) {
      if (crc & 0x8000) {
        crc = ((crc << 1) ^ 0x1021) & 0xFFFF;
      } else {
        crc = (crc << 1) & 0xFFFF;
      }
    }
  }
  return crc;
}

/**
 * Build a binary frame for sending
 * @param {number} cmd 
 * @param {Uint8Array} payload 
 * @returns {ArrayBuffer}
 */
export function buildFrame(cmd, payload = new Uint8Array(0)) {
  const plen = payload.length;
  const buf = new Uint8Array(6 + plen);
  
  buf[0] = PKT_SOF;
  buf[1] = cmd;
  buf[2] = plen & 0xFF;
  buf[3] = (plen >> 8) & 0xFF;
  
  if (plen > 0) {
    buf.set(payload, 4);
  }
  
  const crc = crc16_ccitt(buf.subarray(1, 4 + plen));
  buf[4 + plen] = crc & 0xFF;
  buf[5 + plen] = (crc >> 8) & 0xFF;
  
  return buf.buffer;
}

/**
 * Parse Telemetry Payload (0x90)
 * Format: cmd1(int16), cmd2(int16), speedR(int16), speedL(int16), batV(int16), temp(int16), led(uint16)
 */
export function parseTelemetry(payload) {
  if (payload.byteLength < 14) return null;
  const view = (payload instanceof ArrayBuffer) 
    ? new DataView(payload) 
    : new DataView(payload.buffer, payload.byteOffset, payload.byteLength);
  return {
    cmd1: view.getInt16(0, true),
    cmd2: view.getInt16(2, true),
    speedR: view.getInt16(4, true),
    speedL: view.getInt16(6, true),
    batVoltage: view.getInt16(8, true) / 100.0, // Assume centivolts
    boardTemp: view.getInt16(10, true) / 10.0,  // Assume deci-celsius
    led: view.getUint16(12, true),
    timestamp: Date.now()
  };
}

/**
 * Validate a received frame
 * @param {Uint8Array} bytes 
 * @returns {boolean}
 */
export function validateFrame(bytes) {
  if (bytes.length < 6) return false;
  if (bytes[0] !== PKT_SOF) return false;
  
  const plen = bytes[2] | (bytes[3] << 8);
  if (bytes.length < 6 + plen) return false;
  
  const calcCrc = crc16_ccitt(bytes.subarray(1, 4 + plen));
  const recvCrc = bytes[4 + plen] | (bytes[5 + plen] << 8);
  
  return calcCrc === recvCrc;
}
