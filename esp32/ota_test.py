import serial
import struct
import time
import argparse
import os

# --- 协议常量 (需与 protocol.h 一致) ---
PKT_SOF = 0x7E
CMD_OTA_BEGIN = 0x40
CMD_OTA_DATA  = 0x41
CMD_OTA_END   = 0x42
ACK_MASK      = 0x80

def crc16_ccitt(data):
    crc = 0xFFFF
    for byte in data:
        crc ^= (byte << 8)
        for _ in range(8):
            if crc & 0x8000:
                crc = (crc << 1) ^ 0x1021
            else:
                crc = crc << 1
            crc &= 0xFFFF
    return crc

def build_frame(cmd, payload=b''):
    plen = len(payload)
    # [SOF][CMD][LEN_LE_2][PAYLOAD][CRC_LE_2]
    header = struct.pack('<BBH', PKT_SOF, cmd, plen)
    frame_no_crc = header[1:] + payload # CRC 覆盖 CMD + LEN + Payload
    crc = crc16_ccitt(frame_no_crc)
    return header + payload + struct.pack('<H', crc)

def parse_frame(data):
    if len(data) < 6: return None
    if data[0] != PKT_SOF: return None
    cmd = data[1]
    plen = struct.unpack('<H', data[2:4])[0]
    if len(data) < 6 + plen: return None
    payload = data[4:4+plen]
    return cmd, payload

class OTATester:
    def __init__(self, port, baud=115200):
        self.ser = serial.Serial(port, baud, timeout=1)
        
    def send_and_wait_ack(self, cmd, payload=b'', timeout=2):
        frame = build_frame(cmd, payload)
        self.ser.write(frame)
        
        start_time = time.time()
        buf = bytearray()
        while time.time() - start_time < timeout:
            if self.ser.in_waiting > 0:
                buf.extend(self.ser.read(self.ser.in_waiting))
            
            # 在缓冲区中寻找 SOF
            while len(buf) >= 6:
                if buf[0] == PKT_SOF:
                    res = parse_frame(buf)
                    if res:
                        parsed_cmd, parsed_payload = res
                        if parsed_cmd == (cmd | ACK_MASK):
                            return parsed_payload
                        # 如果不是我们要的 ACK，可能是别的包，删掉这一帧继续找
                        plen = struct.unpack('<H', buf[2:4])[0]
                        buf = buf[6+plen:]
                        continue
                buf.pop(0) # 扔掉非 SOF 字节
            time.sleep(0.01)
        return None

    def run_ota(self, file_path):
        if not os.path.exists(file_path):
            print(f"Error: File {file_path} not found")
            return

        with open(file_path, 'rb') as f:
            bin_data = f.read()
        
        total_size = len(bin_data)
        print(f"[*] Starting OTA for {file_path}")
        print(f"[*] Total Size: {total_size} bytes")

        # 1. OTA_BEGIN
        # Payload: [size:uint32][type:uint8] (0=ESP32)
        begin_payload = struct.pack('<IB', total_size, 0)
        print("[>] Sending OTA_BEGIN...")
        res = self.send_and_wait_ack(CMD_OTA_BEGIN, begin_payload)
        if res is None:
            print("[!] No response to OTA_BEGIN. Is ESP32 running and connected?")
            return
        print("[+] OTA_BEGIN Accepted")

        # 2. OTA_DATA
        chunk_size = 240 # 接近 MAX_PAYLOAD_LEN (256)
        offset = 0
        start_time = time.time()
        
        while offset < total_size:
            end = min(offset + chunk_size, total_size)
            chunk = bin_data[offset:end]
            
            # Payload: [offset:uint32][data:N]
            data_payload = struct.pack('<I', offset) + chunk
            res = self.send_and_wait_ack(CMD_OTA_DATA, data_payload)
            
            if res:
                received_offset = struct.unpack('<I', res[0:4])[0]
                offset = received_offset
                progress = (offset / total_size) * 100
                elapsed = time.time() - start_time
                speed = (offset / 1024) / elapsed if elapsed > 0 else 0
                print(f"\rProgress: {progress:.1f}% | Speed: {speed:.1f} KB/s", end='')
            else:
                print(f"\n[!] Timeout at offset {offset}. Retrying...")
                # 实际生产环境应增加重试次数限制
        
        print("\n[+] Data transfer complete")

        # 3. OTA_END
        print("[>] Sending OTA_END...")
        res = self.send_and_wait_ack(CMD_OTA_END)
        if res is not None:
            print("[+] OTA Finished Successfully! ESP32 should reboot now.")
        else:
            print("[!] OTA_END timed out. Check ESP32 serial logs.")

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description='ESP32 OTA Serial Tester')
    parser.add_argument('--port', default='/dev/ttyUSB0', help='Serial port')
    parser.add_argument('--file', required=True, help='Path to firmware.bin')
    args = parser.parse_args()

    tester = OTATester(args.port)
    tester.run_ota(args.file)
