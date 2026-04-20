import asyncio
from bleak import BleakScanner

async def run():
    print("Scanning for BLE devices (20s)...")
    devices = await BleakScanner.discover(timeout=20.0)
    for d in devices:
        print(f"Found: {d.name} [{d.address}]")

if __name__ == "__main__":
    asyncio.run(run())
