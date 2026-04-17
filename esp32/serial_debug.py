import serial
import time
import struct
import argparse
import threading
import sys

# ============================================================
# Hoverboard Serial Debugger (ESP32 Bridge Mode)
# ============================================================
# 使用描述：
# sudo python3 serial_debug.py <drive> <speed>
# 功能描述:
# drive <speed> 按指定速度和方向驱动
# load_bin <bin_path> 将指定的bin文件发送给esp32,esp32将bin文件存储起来。
# ota 将存储的bin文件进行ota升级。并汇报升级进度。
# info 显示温度速度电压等信息。
# ============================================================

PKT_SOF = 0x7E
CMD_DRIVE = 0x10
CMD_TELEMETRY = 0x90

def crc16_ccitt(data):
    crc = 0xFFFF
    for byte in data:
        crc ^= (byte << 8)
        for _ in range(8):
            if crc & 0x8000:
                crc = (crc << 1) ^ 0x1021
            else:
                crc <<= 1
            crc &= 0xFFFF
    return crc

def build_protocol_c_frame(cmd, payload):
    plen = len(payload)
    # [SOF:1][CMD:1][LEN:2][PAYLOAD:N][CRC:2]
    header = struct.pack('<BBH', PKT_SOF, cmd, plen)
    frame_no_crc = header[1:] + payload # CRC covers CMD+LEN+PAYLOAD
    crc = crc16_ccitt(frame_no_crc)
    return header + payload + struct.pack('<H', crc)

def parse_telemetry(payload):
    if len(payload) < 14:
        return None
    try:
        cmd1, cmd2, spdR, spdL, batV, temp, led = struct.unpack('<hhhhhhH', payload[:14])
        return {
            "cmd1": cmd1, "cmd2": cmd2, "speedR": spdR, "speedL": spdL,
            "voltage": batV / 100.0, "temp": temp / 10.0, "led": hex(led)
        }
    except Exception:
        return None

def parse_protocol_ac(data):
    if len(data) < 6: return None
    cmd = data[1]
    plen = struct.unpack('<H', data[2:4])[0]
    if len(data) < 6 + plen: return None
    
    payload = data[4:4+plen]
    checksum_recv = struct.unpack('<H', data[4+plen:6+plen])[0]
    checksum_calc = crc16_ccitt(data[1:4+plen])
    
    res = {"type": "A/C", "cmd": cmd, "len": plen, "payload": payload, "crc_ok": checksum_recv == checksum_calc}
    if cmd == CMD_TELEMETRY:
        tele = parse_telemetry(payload)
        if tele: res["telemetry"] = tele
    return res

def reader_thread(ser):
    buffer = bytearray()
    while ser.is_open:
        try:
            if ser.in_waiting > 0:
                buffer.extend(ser.read(ser.in_waiting))
            
            while len(buffer) >= 6:
                if buffer[0] == PKT_SOF:
                    plen = struct.unpack('<H', buffer[2:4])[0]
                    total_len = 6 + plen
                    if len(buffer) < total_len: break
                    
                    res = parse_protocol_ac(buffer[:total_len])
                    buffer = buffer[total_len:]

                    if res and res["crc_ok"]:
                        if "telemetry" in res:
                            t = res["telemetry"]
                            print(f"\r[{time.strftime('%H:%M:%S')}] FB | R:{t['speedR']:4d} L:{t['speedL']:4d} RPM | {t['voltage']:.1f}V | {t['temp']:.1f}C | CMD:{t['cmd1']}/{t['cmd2']}\n>> ", end="", flush=True)
                    continue
                buffer.pop(0)
            time.sleep(0.01)
        except Exception as e:
            print(f"Reader Error: {e}")
            break

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", default="/dev/ttyACM0")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--speed", type=int, default=50, help="Initial speed for auto-start")
    args = parser.parse_args()

    print(f"--- Hoverboard ESP32 Bridge Tool (Auto-Test) ---")
    try:
        ser = serial.Serial(args.port, args.baud, timeout=0.1)
    except Exception as e:
        print(f"Error: {e}"); return

    # 1. 启动解析线程
    t = threading.Thread(target=reader_thread, args=(ser,), daemon=True)
    t.start()

    # 2. 自动发送慢速启动指令
    print(f"[AUTO] Sending auto-start command: Steer=0, Speed={args.speed}...")
    payload = struct.pack('<hh', 0, args.speed)
    frame = build_protocol_c_frame(CMD_DRIVE, payload)
    ser.write(frame)
    ser.flush()

    print("Commands: 'drive <steer> <speed>', 'stop', 'quit'")
    print("Watching telemetry (Press CTRL+C or type 'quit' to stop)...")
    
    try:
        while True:
            # 使用非阻塞方式读取输入，以便同时显示反馈
            cmd_line = input(">> ").strip().lower()
            if not cmd_line: continue
            if cmd_line == "quit": break
            
            parts = cmd_line.split()
            if parts[0] == "drive":
                if len(parts) < 3: continue
                steer, speed = int(parts[1]), int(parts[2])
                payload = struct.pack('<hh', steer, speed)
                frame = build_protocol_c_frame(CMD_DRIVE, payload)
                ser.write(frame)
                print(f"[SEND] Drive Steer={steer} Speed={speed}")
            elif parts[0] == "stop":
                payload = struct.pack('<hh', 0, 0)
                frame = build_protocol_c_frame(CMD_DRIVE, payload)
                ser.write(frame)
                print("[SEND] Stop")
    except KeyboardInterrupt:
        pass
    finally:
        # 退出前停止电机
        stop_payload = struct.pack('<hh', 0, 0)
        stop_frame = build_protocol_c_frame(CMD_DRIVE, stop_payload)
        ser.write(stop_frame)
        ser.close()
        print("\n[FINISH] Stopped motors and closed port.")

if __name__ == "__main__":
    main()
