import struct
import time
import argparse
import asyncio
import sys

try:
    from bleak import BleakClient, BleakScanner
    HAS_BLEAK = True
except ImportError:
    HAS_BLEAK = False

# Protocol C Definitions
PKT_SOF = 0x7E
CMD_PING = 0x01
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

def parse_frame(data):
    if len(data) < 6:
        return None, data
    # Find SOF
    try:
        sof_idx = data.index(PKT_SOF)
        if sof_idx > 0:
            data = data[sof_idx:]
    except ValueError:
        return None, b''
    
    if len(data) < 6:
        return None, data
        
    cmd = data[1]
    length = struct.unpack('<H', data[2:4])[0]
    frame_len = 6 + length
    
    if len(data) < frame_len:
        return None, data
        
    frame = data[:frame_len]
    payload = frame[4:4+length]
    recv_crc = struct.unpack('<H', frame[-2:])[0]
    calc_crc = crc16_ccitt(frame[1:-2])
    
    if recv_crc != calc_crc:
        print(f"CRC Error! Calc: {hex(calc_crc)}, Recv: {hex(recv_crc)}")
        return None, data[1:]
        
    return (cmd, payload), data[frame_len:]

# BLE State
buffer = bytearray()
auth_event = asyncio.Event()
client = None

async def notification_handler(sender, data):
    global buffer, auth_event, client
    buffer.extend(data)
    
    while len(buffer) >= 6:
        result, new_buf = parse_frame(buffer)
        buffer = bytearray(new_buf)
        if result:
            cmd, payload = result
            print(f"Received Frame -> CMD: {hex(cmd)}, Len: {len(payload)}")
            
            if cmd == CMD_AUTH_REQ:
                if len(payload) == 4:
                    challenge = struct.unpack('<I', payload)[0]
                    print(f"  [AUTH REQ] Challenge: 0x{challenge:08X}")
                    response = challenge ^ 0x12345678
                    res_payload = struct.pack('<I', response)
                    res_frame = build_frame(CMD_AUTH_RES, res_payload)
                    if client:
                        await client.write_gatt_char(NUS_RX_UUID, res_frame)
                    print(f"  [AUTH RES] Sent Response: 0x{response:08X}")
            elif cmd == (CMD_AUTH_RES | ACK_MASK):
                print(f"  [AUTH ACK] Authentication Successful!")
                auth_event.set()
            elif cmd == CMD_STATUS | ACK_MASK:
                print(f"  [STATUS ACK] SystemState: {payload[0]}")
            elif cmd == CMD_CONFIG | ACK_MASK:
                print(f"  [CONFIG ACK] Success")
            elif cmd == CMD_TELEMETRY:
                if len(payload) == 14:
                    data_tuple = struct.unpack('<hhhhhhH', payload)
                    print(f"  [TELEMETRY] speedR={data_tuple[2]}, speedL={data_tuple[3]}, V={data_tuple[4]}, Temp={data_tuple[5]}")
                else:
                    print(f"  [TELEMETRY] Invalid length: {len(payload)}")
            else:
                print(f"  [OTHER] CMD ID: {hex(cmd)}")

async def test_ble(device_name):
    if not HAS_BLEAK:
        print("Error: 'bleak' module is required for BLE testing.")
        print("Install it via: pip install bleak")
        sys.exit(1)

    print(f"Scanning for {device_name}...")
    device = await BleakScanner.find_device_by_filter(
        lambda d, ad: d.name and d.name == device_name,
        timeout=10.0
    )
    if not device:
        print(f"Device '{device_name}' not found!")
        return
        
    print(f"Found {device.name} [{device.address}]. Connecting...")
    
    global client
    async with BleakClient(device) as ble_client:
        client = ble_client
        print("Connected! Initializing Notifications...")
        await client.start_notify(NUS_TX_UUID, notification_handler)
        
        print("Requesting AUTH_REQ...")
        await client.write_gatt_char(NUS_RX_UUID, build_frame(CMD_AUTH_REQ))
        
        try:
            # Wait for authentication to complete
            await asyncio.wait_for(auth_event.wait(), timeout=5.0)
            
            # --- START TESTING AFTER AUTHENTICATION ---
            
            # 1. PING Test
            print("\n--- Testing PING ---")
            await client.write_gatt_char(NUS_RX_UUID, build_frame(CMD_PING))
            await asyncio.sleep(0.5)

            # 2. STATUS Test
            print("\n--- Testing STATUS ---")
            await client.write_gatt_char(NUS_RX_UUID, build_frame(CMD_STATUS))
            await asyncio.sleep(0.5)

            # 3. DRIVE Test (Send 3 times)
            print("\n--- Testing DRIVE (Steering=100, Speed=200) ---")
            drive_payload = struct.pack('<hh', 100, 200)
            for _ in range(3):
                await client.write_gatt_char(NUS_RX_UUID, build_frame(CMD_DRIVE, drive_payload))
                await asyncio.sleep(0.2)
            
            # Wait to receive Telemetry
            print("\nWaiting 2 seconds to collect Telemetry...")
            await asyncio.sleep(2.0)
        
        except asyncio.TimeoutError:
            print("Authentication Timeout! Did the device send AUTH_REQ?")
        except Exception as e:
            print(f"Error during BLE test: {e}")
        finally:
            print("Disconnecting...")
            await client.stop_notify(NUS_TX_UUID)

def test_serial(port_name, baud):
    import serial
    print(f"Opening port {port_name} at {baud} baud...")
    try:
        ser = serial.Serial(port_name, baud, timeout=1.0)
    except Exception as e:
        print(f"Error opening port: {e}")
        return

    # Serial adapter has no authentication wall, so we test directly
    print("\n--- Testing PING ---")
    ser.write(build_frame(CMD_PING))
    time.sleep(0.1)

    print("\n--- Testing STATUS ---")
    ser.write(build_frame(CMD_STATUS))
    time.sleep(0.1)

    print("\n--- Testing DRIVE ---")
    ser.write(build_frame(CMD_DRIVE, struct.pack('<hh', 100, 200)))
    time.sleep(0.1)
    
    timeout_start = time.time()
    ser_buffer = b''
    while time.time() - timeout_start < 2.0:
        if ser.in_waiting > 0:
            ser_buffer += ser.read(ser.in_waiting)
        while len(ser_buffer) >= 6:
            result, ser_buffer = parse_frame(ser_buffer)
            if result:
                cmd, payload = result
                print(f"Received Frame -> CMD: {hex(cmd)}")
        time.sleep(0.01)

    ser.close()

def main():
    parser = argparse.ArgumentParser(description="Hoverboard ESP32 Protocol C Test")
    parser.add_argument("--mode", choices=['ble', 'serial'], default='ble', help="Test mode: 'ble' or 'serial'")
    parser.add_argument("--name", type=str, default="HoverBoard-OTA", help="BLE Device Name to connect to")
    parser.add_argument("--port", type=str, default="/dev/ttyACM0", help="Serial port (only used in serial mode)")
    parser.add_argument("--baud", type=int, default=115200, help="Baud rate (only used in serial mode)")
    args = parser.parse_args()

    if args.mode == 'ble':
        asyncio.run(test_ble(args.name))
    else:
        test_serial(args.port, args.baud)

if __name__ == "__main__":
    main()
