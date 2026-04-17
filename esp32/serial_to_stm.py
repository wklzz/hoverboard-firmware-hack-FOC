import serial
import time
import struct
import argparse
import sys

# ============================================================
# Hoverboard Direct STM32 Debug Tool (Protocol B)
# ============================================================
# 用于通过 CP2102 直接向 STM32 发送 Protocol B 控制指令。
# 注意：这跳过了 ESP32，直接模拟遥控信号。
# ============================================================

RUNTIME_SOF = 0xABCD

def calc_xor(sof, steer, speed):
    return (sof ^ (steer & 0xFFFF) ^ (speed & 0xFFFF)) & 0xFFFF

def build_protocol_b_frame(steer, speed):
    xor = calc_xor(RUNTIME_SOF, steer, speed)
    # [SOF:H][steer:h][speed:h][xor:H]
    return struct.pack('<HhhH', RUNTIME_SOF, steer, speed, xor)

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", default="/dev/ttyUSB0", help="CP2102 Port")
    parser.add_argument("--baud", type=int, default=115200, help="Baud rate")
    args = parser.parse_args()

    print(f"--- Hoverboard Direct STM32 Tool ---")
    print(f"Target: {args.port} at {args.baud} baud")
    
    try:
        ser = serial.Serial(args.port, args.baud, timeout=0.1, dsrdtr=False, rtscts=False)
    except Exception as e:
        print(f"Error: {e}")
        return

    print("Commands:")
    print("  drive <steer> <speed> - Send direct Protocol B control")
    print("  cycle                 - 1s move / 1s stop loop")
    print("  stop                  - Stop immediately")
    print("  quit                  - Exit")
    print("-" * 40)

    try:
        while True:
            cmd_line = input(">> ").strip().lower()
            if not cmd_line: continue
            if cmd_line == "quit": break

            parts = cmd_line.split()
            cmd_name = parts[0]
            
            if cmd_name == "drive":
                if len(parts) < 3:
                    print("Usage: drive <steer> <speed>")
                    continue
                steer, speed = int(parts[1]), int(parts[2])
                frame = build_protocol_b_frame(steer, speed)
                ser.write(frame)
                ser.flush()
                print(f"[SEND B] Steer: {steer}, Speed: {speed} | HEX: {frame.hex()}")
            
            elif cmd_name == "stop":
                frame = build_protocol_b_frame(0, 0)
                ser.write(frame)
                ser.flush()
                print(f"[STOP B] Sent 0/0")

            elif cmd_name == "cycle":
                print("Starting Cycle: 1s Move (50), 1s Stop. CTRL+C to break.")
                try:
                    while True:
                        # Move
                        f_move = build_protocol_b_frame(0, 50)
                        ser.write(f_move)
                        ser.flush()
                        print("\r[CYCLE] Moving... ", end="", flush=True)
                        time.sleep(1.0)
                        # Stop
                        f_stop = build_protocol_b_frame(0, 0)
                        ser.write(f_stop)
                        ser.flush()
                        print("\r[CYCLE] Stopped. ", end="", flush=True)
                        time.sleep(1.0)
                except KeyboardInterrupt:
                    print("\nCycle stopped.")
    except Exception as e:
        print(f"Error during execution: {e}")
    finally:
        ser.close()
        print("Closed.")

if __name__ == "__main__":
    main()
