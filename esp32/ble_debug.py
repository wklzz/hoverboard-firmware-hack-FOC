import struct
import time
import argparse
import asyncio
import sys
import os

try:
    from bleak import BleakClient, BleakScanner
    HAS_BLEAK = True
except ImportError:
    HAS_BLEAK = False

# ============================================================
# Hoverboard BLE Debugger (ESP32 BLE)
# ============================================================
# 使用描述：
# sudo python3 ble_debug.py 
# (或 sudo python3 ble_debug.py --name HoverBoard-OTA)
# 功能描述 (进入交互界面后输入):
# drive <speed> 按指定速度和方向驱动
# load_bin <bin_path> 将指定的bin文件发送给esp32,esp32将bin文件存储起来 (即读取到电脑内存作为 OTA 准备)。
# ota 将存储的bin文件进行ota升级。并汇报升级进度。
# info 显示温度速度电压等信息。
# ============================================================

PKT_SOF = 0x7E

# Protocol A/C commands
CMD_PING = 0x01
CMD_INFO = 0x02
CMD_ERASE = 0x03
CMD_WRITE = 0x04
CMD_BOOT = 0x05

CMD_DRIVE = 0x10
CMD_STATUS = 0x11
CMD_CONFIG = 0x20
CMD_AUTH_REQ = 0x30
CMD_AUTH_RES = 0x31
CMD_TELEMETRY = 0x90
ACK_MASK = 0x80

NUS_RX_UUID = "6e400002-b5a3-f393-e0a9-e50e24dcca9e"
NUS_TX_UUID = "6e400003-b5a3-f393-e0a9-e50e24dcca9e"

def crc16_ccitt(data):
    crc = 0xFFFF
    for byte in data:
        crc ^= (byte << 8) & 0xFFFF
        for _ in range(8):
            if crc & 0x8000:
                crc = ((crc << 1) ^ 0x1021) & 0xFFFF
            else:
                crc = (crc << 1) & 0xFFFF
    return crc

def build_frame(cmd, payload=b''):
    length = len(payload)
    frame = struct.pack('<BBB', PKT_SOF, cmd, length & 0xFF) + struct.pack('<B', (length >> 8) & 0xFF) + payload
    crc_data = frame[1:]
    crc = crc16_ccitt(crc_data)
    frame += struct.pack('<H', crc)
    return frame

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

# Global state
buffer = bytearray()
client = None
auth_event = asyncio.Event()
response_event = asyncio.Event()
last_response = None
ota_firmware_data = None
show_info = False

async def notification_handler(sender, data):
    global buffer, auth_event, client, last_response, response_event, show_info
    buffer.extend(data)
    
    while len(buffer) >= 6:
        try:
            sof_idx = buffer.index(PKT_SOF)
            if sof_idx > 0:
                buffer = buffer[sof_idx:]
        except ValueError:
            buffer.clear()
            break
            
        if len(buffer) < 6:
            break
            
        cmd = buffer[1]
        length = struct.unpack('<H', buffer[2:4])[0]
        frame_len = 6 + length
        
        if len(buffer) < frame_len:
            break
            
        frame = buffer[:frame_len]
        payload = frame[4:4+length]
        recv_crc = struct.unpack('<H', frame[-2:])[0]
        calc_crc = crc16_ccitt(frame[1:-2])
        
        buffer = buffer[frame_len:]
        
        if recv_crc != calc_crc:
            # ignore silently if CRC is wrong and wait next complete frame
            continue
            
        # Handle command
        if cmd == CMD_AUTH_REQ:
            if len(payload) == 4:
                challenge = struct.unpack('<I', payload)[0]
                response = challenge ^ 0x12345678
                res_payload = struct.pack('<I', response)
                res_frame = build_frame(CMD_AUTH_RES, res_payload)
                if client:
                    asyncio.create_task(client.write_gatt_char(NUS_RX_UUID, res_frame))
        elif cmd == (CMD_AUTH_RES | ACK_MASK):
            auth_event.set()
        elif cmd == CMD_TELEMETRY:
            if show_info:
                tele = parse_telemetry(payload)
                if tele:
                    print(f"\r[INFO] R:{tele['speedR']:4d} L:{tele['speedL']:4d} RPM | {tele['voltage']:.1f}V | {tele['temp']:.1f}C ", end="", flush=True)
        else:
            last_response = (cmd, payload)
            response_event.set()

async def read_input():
    loop = asyncio.get_event_loop()
    return await loop.run_in_executor(None, sys.stdin.readline)

async def wait_response(expected_cmd, timeout=2.0):
    global last_response, response_event
    response_event.clear()
    try:
        await asyncio.wait_for(response_event.wait(), timeout)
        if last_response and last_response[0] == expected_cmd:
            return last_response[1]
    except asyncio.TimeoutError:
        pass
    return None

async def cmd_loop():
    global ota_firmware_data, show_info, client
    print("\nCommands: 'drive <steer> <speed>', 'load_bin <file>', 'ota', 'info', 'stop', 'quit'")
    
    while True:
        try:
            line = await read_input()
            if not line:
                continue
            line = line.strip()
            if not line:
                continue
            
            parts = line.split()
            cmd = parts[0].lower()
            
            if cmd == "quit":
                break
                
            elif cmd == "drive":
                show_info = False
                if len(parts) >= 2:
                    steer = 0
                    speed = int(parts[1])
                    if len(parts) >= 3:
                        steer = int(parts[1])
                        speed = int(parts[2])
                    payload = struct.pack('<hh', steer, speed)
                    await client.write_gatt_char(NUS_RX_UUID, build_frame(CMD_DRIVE, payload))
                    print(f"[SEND] Drive Steer={steer} Speed={speed}")
                else:
                    print("Usage: drive <steer> <speed> OR drive <speed>")
                    
            elif cmd == "stop":
                show_info = False
                payload = struct.pack('<hh', 0, 0)
                await client.write_gatt_char(NUS_RX_UUID, build_frame(CMD_DRIVE, payload))
                print("[SEND] Stop")
                
            elif cmd == "info":
                show_info = not show_info
                if show_info:
                    print("\n[INFO] Requesting ESP32 Status...")
                    await client.write_gatt_char(NUS_RX_UUID, build_frame(CMD_STATUS))
                    resp = await wait_response(CMD_STATUS | ACK_MASK, 2.0)
                    if resp:
                        # State: 0=IDLE, 1=RUNNING, 2=OTA
                        state = resp[0]
                        state_names = {0: "IDLE", 1: "RUNNING", 2: "OTA"}
                        mode = resp[1] if len(resp) > 1 else 0
                        mode_str = "BLE/WiFi" if mode == 0 else "PWM"
                        print(f"\n[STATUS] State: {state_names.get(state, 'Unknown')} ({state}) | Control Mode: {mode} ({mode_str})")
                        print(f"[STATUS] Full Payload: {resp.hex().upper()}")
                    else:
                        print("\n[INFO] Warning: ESP32 status query timed out after 2s.")
                    print("[INFO] Displaying telemetry... (Type 'info' again to stop)")
                else:
                    print("\n[INFO] Stopped displaying telemetry.")

            elif cmd == "mode":
                show_info = False
                if len(parts) >= 2:
                    m = int(parts[1])
                    payload = struct.pack('<BB', 0x01, m)
                    await client.write_gatt_char(NUS_RX_UUID, build_frame(CMD_CONFIG, payload))
                    print(f"[SEND] Mode switch request to: {m} (0=BLE/WiFi, 1=PWM)")
                else:
                    print("Usage: mode <num> (e.g., mode 1 for PWM)")
                    
            elif cmd == "load_bin":
                show_info = False
                if len(parts) >= 2:
                    bin_path = parts[1]
                    try:
                        with open(bin_path, "rb") as f:
                            ota_firmware_data = f.read()
                        print(f"[LOAD] Successfully loaded {bin_path} ({len(ota_firmware_data)} bytes)")
                    except Exception as e:
                        print(f"[ERROR] Could not load file: {e}")
                else:
                    print("Usage: load_bin <bin_path>")
                    
            elif cmd == "ota":
                show_info = False
                if not ota_firmware_data:
                    print("[ERROR] No firmware loaded. Use 'load_bin <bin_path>' first.")
                    continue
                
                print("[OTA] Starting OTA process...")
                
                # Ping - also triggers ESP32 to reboot STM32 if not already OTA
                await client.write_gatt_char(NUS_RX_UUID, build_frame(CMD_PING))
                resp = await wait_response(CMD_PING | ACK_MASK, 3.0)
                if not resp:
                    print("[OTA ERROR] Bootloader not responding to PING. Retrying...")
                    await client.write_gatt_char(NUS_RX_UUID, build_frame(CMD_PING))
                    resp = await wait_response(CMD_PING | ACK_MASK, 3.0)
                    if not resp:
                        print("[OTA ERROR] Bootloader still not responding. Aborting.")
                        continue
                print(f"[OTA] Ping OK (Version: {resp[0]}.{resp[1]})")
                
                # Info
                await client.write_gatt_char(NUS_RX_UUID, build_frame(CMD_INFO))
                resp = await wait_response(CMD_INFO | ACK_MASK, 3.0)
                if not resp or len(resp) < 8:
                    print("[OTA ERROR] Failed to get INFO.")
                    continue
                app_base, app_max = struct.unpack('<II', resp[:8])
                print(f"[OTA] Info OK (Base: 0x{app_base:08X}, Max Size: {app_max} bytes)")
                
                if len(ota_firmware_data) > app_max:
                    print(f"[OTA ERROR] Firmware too large ({len(ota_firmware_data)} > {app_max})")
                    continue
                    
                # Erase
                print("[OTA] Erasing flash... (This may take a few seconds)")
                await client.write_gatt_char(NUS_RX_UUID, build_frame(CMD_ERASE))
                resp = await wait_response(CMD_ERASE | ACK_MASK, 15.0)
                if not resp or resp[0] != 0:
                    print(f"[OTA ERROR] Erase failed! (Resp: {resp})")
                    continue
                print("[OTA] Erase OK")
                
                # Write
                print("[OTA] Writing firmware...")
                chunk_size = 128
                total_bytes = len(ota_firmware_data)
                addr = app_base
                
                success = True
                for i in range(0, total_bytes, chunk_size):
                    chunk = ota_firmware_data[i:i+chunk_size]
                    payload = struct.pack('<IH', addr, len(chunk)) + chunk
                    
                    retry = 3
                    while retry > 0:
                        await client.write_gatt_char(NUS_RX_UUID, build_frame(CMD_WRITE, payload))
                        resp = await wait_response(CMD_WRITE | ACK_MASK, 2.0)
                        if resp and resp[0] == 0:
                            break
                        retry -= 1
                    
                    if retry == 0:
                        print(f"\n[OTA ERROR] Write failed at offset {i}. (Resp: {resp})")
                        success = False
                        break
                        
                    addr += len(chunk)
                    progress = min(100, int((i + len(chunk)) / total_bytes * 100))
                    print(f"\r[OTA] Progress: {progress}% ({i + len(chunk)}/{total_bytes} bytes)", end="", flush=True)
                    
                print()
                if not success:
                    continue
                    
                # Boot
                print("[OTA] Booting...")
                await client.write_gatt_char(NUS_RX_UUID, build_frame(CMD_BOOT))
                resp = await wait_response(CMD_BOOT | ACK_MASK, 2.0)
                if resp and resp[0] == 0:
                    print("[OTA] Update completed successfully! Hoverboard is rebooting.")
                else:
                    print("[OTA ERROR] Boot command failed or no response.")
                    
            else:
                print(f"Unknown command: {cmd}")
                
        except Exception as e:
            print(f"Error in command loop: {e}")

async def main(device_name):
    global client
    if not HAS_BLEAK:
        print("Error: 'bleak' module is required. Run 'pip install bleak'")
        sys.exit(1)
        
    print(f"Scanning for {device_name}...")
    device = await BleakScanner.find_device_by_filter(
        lambda d, ad: d.name and d.name == device_name,
        timeout=5.0
    )
    
    if not device:
        print(f"Device '{device_name}' not found!")
        return
        
    try:
        async with BleakClient(device) as ble_client:
            client = ble_client
            print(f"Connected to {device.address}! Performing Service Discovery...")
            # services 属性在 connect 后自动包含已发现的服务
            services = client.services
            
            # 校验 NUS 服务是否存在 (使用 UUID 字符串)
            if not services.get_characteristic(NUS_RX_UUID):
                 print(f"[ERROR] NUS RX characteristic ({NUS_RX_UUID}) not found on device!")
                 return
            
            print("Service Discovery Complete. Initializing Notifications...")
            await client.start_notify(NUS_TX_UUID, notification_handler)
            
            # 给一点稳定时间
            await asyncio.sleep(1.0)
            
            print("Requesting Authentication...")
            await client.write_gatt_char(NUS_RX_UUID, build_frame(CMD_AUTH_REQ))
            
            try:
                await asyncio.wait_for(auth_event.wait(), timeout=5.0)
                print("Authentication Successful!")
                
                # Start CLI loop
                await cmd_loop()
                
            except asyncio.TimeoutError:
                print("Authentication Timeout! Could not authenticate with ESP32.")
            
            print("Disconnecting...")
            await client.stop_notify(NUS_TX_UUID)
            
    except Exception as e:
        print(f"Connection error: {e}")

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Hoverboard ESP32 BLE Debugger")
    parser.add_argument("--name", type=str, default="HoverBoard-OTA", help="BLE Device Name to connect to")
    args = parser.parse_args()

    asyncio.run(main(args.name))