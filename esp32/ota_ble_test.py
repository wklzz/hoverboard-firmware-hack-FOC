import asyncio
import struct
import os
import argparse
from bleak import BleakClient, BleakScanner

# --- BLE UUIDs (需与 BLEAdapter.h 一致) ---
NUS_SERVICE_UUID = "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
NUS_RX_UUID      = "6E400002-B5A3-F393-E0A9-E50E24DCCA9E" # Write
NUS_TX_UUID      = "6E400003-B5A3-F393-E0A9-E50E24DCCA9E" # Notify

# --- 协议常量 ---
PKT_SOF = 0x7E
CMD_AUTH_REQ = 0x30
CMD_AUTH_RES = 0x31
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
    header = struct.pack('<BBH', PKT_SOF, cmd, plen)
    frame_no_crc = header[1:] + payload
    crc = crc16_ccitt(frame_no_crc)
    return header + payload + struct.pack('<H', crc)

class BLEOTATester:
    def __init__(self, target_name, file_path):
        self.target_name = target_name
        self.file_path = file_path
        self.client = None
        self.authenticated = asyncio.Event()
        self.ack_event = asyncio.Event()
        self.last_ack_payload = None

    def notification_handler(self, sender, data):
        if len(data) < 6 or data[0] != PKT_SOF:
            return

        cmd = data[1]
        plen = struct.unpack('<H', data[2:4])[0]
        payload = data[4:4+plen]

        if cmd == CMD_AUTH_REQ:
            challenge = struct.unpack('<I', payload)[0]
            print(f"[*] Received Auth Challenge: 0x{challenge:08X}")
            asyncio.create_task(self.send_auth_response(challenge))
        
        elif cmd == (CMD_AUTH_RES | ACK_MASK):
            print("[+] Authentication Successful!")
            self.authenticated.set()
        
        elif cmd & ACK_MASK:
            self.last_ack_payload = payload
            self.ack_event.set()

    async def send_auth_response(self, challenge):
        response = challenge ^ 0x12345678
        print(f"[*] Sending Auth Response: 0x{response:08X}")
        frame = build_frame(CMD_AUTH_RES, struct.pack('<I', response))
        await self.client.write_gatt_char(NUS_RX_UUID, frame)

    async def send_and_wait_ack(self, cmd, payload=b''):
        self.ack_event.clear()
        frame = build_frame(cmd, payload)
        await self.client.write_gatt_char(NUS_RX_UUID, frame)
        try:
            await asyncio.wait_for(self.ack_event.wait(), timeout=5.0)
            return self.last_ack_payload
        except asyncio.TimeoutError:
            return None

    async def run(self):
        print(f"[*] Scanning for device: {self.target_name}...")
        if ":" in self.target_name:
            device = await BleakScanner.find_device_by_address(self.target_name)
        else:
            device = await BleakScanner.find_device_by_name(self.target_name)
            
        if not device:
            print("[!] Device not found")
            return

        print(f"[*] Connecting to {device.address}...")
        async with BleakClient(device) as client:
            self.client = client
            print("[+] Connected!")
            
            await client.start_notify(NUS_TX_UUID, self.notification_handler)
            
            print("[*] Requesting Authentication Challenge...")
            req_frame = build_frame(CMD_AUTH_REQ)
            await client.write_gatt_char(NUS_RX_UUID, req_frame)

            print("[*] Waiting for Challenge and Authentication...")
            try:
                await asyncio.wait_for(self.authenticated.wait(), timeout=10.0)
            except asyncio.TimeoutError:
                print("[!] Authentication timed out. Check ESP32 logs.")
                return

            with open(self.file_path, 'rb') as f:
                bin_data = f.read()
            
            total_size = len(bin_data)
            print(f"[*] Starting OTA: {total_size} bytes")

            # OTA_BEGIN
            res = await self.send_and_wait_ack(CMD_OTA_BEGIN, struct.pack('<IB', total_size, 0))
            if res is None:
                print("[!] OTA_BEGIN failed")
                return
            print("[+] OTA_BEGIN Accepted")

            chunk_size = 240 # BLE MTU 限制，建议不要太大
            offset = 0
            while offset < total_size:
                end = min(offset + chunk_size, total_size)
                chunk = bin_data[offset:end]
                res = await self.send_and_wait_ack(CMD_OTA_DATA, struct.pack('<I', offset) + chunk)
                if res:
                    offset = struct.unpack('<I', res[0:4])[0]
                    print(f"\rProgress: {(offset/total_size)*100:.1f}%", end='')
                else:
                    print(f"\n[!] Timeout at offset {offset}")
                    break

            print("\n[*] Finishing OTA...")
            await self.send_and_wait_ack(CMD_OTA_END)
            print("[+] Done! Device should reboot.")

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description='ESP32 OTA BLE Tester')
    parser.add_argument('--name', default='HoverBoard-OTA', help='BLE Device Name')
    parser.add_argument('--address', help='BLE Device MAC Address')
    parser.add_argument('--file', required=True, help='Path to firmware.bin')
    args = parser.parse_args()

    target = args.address if args.address else args.name
    tester = BLEOTATester(target, args.file)
    asyncio.run(tester.run())
