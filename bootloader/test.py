import serial
import struct
import time
import sys
import argparse

# --- 配置区 ---
SERIAL_PORT = '/dev/ttyUSB0'  # 你的串口设备
BAUDRATE = 115200
TIMEOUT = 2                   # 串口超时时间
CHUNK_SIZE = 128              # 固件每包发送的大小（与 main.c PACKET_MAX_PAYLOAD 对应）

# 命令定义 (与 main.c 保持一致)
CMD_PING  = 0x01
CMD_INFO  = 0x02
CMD_ERASE = 0x03
CMD_WRITE = 0x04
CMD_BOOT  = 0x05
ACK_MASK  = 0x80

def crc16_ccitt(data):
    """计算 CRC16-CCITT (符合 STM32 代码逻辑)"""
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

class HoverBootloaderClient:
    def __init__(self, port, baud):
        self.ser = serial.Serial(port, baud, timeout=TIMEOUT)
        
    def send_packet(self, cmd, payload=b''):
        """构造并发送数据包: [0x7E] [CMD] [LEN_L] [LEN_H] [Payload] [CRC_L] [CRC_H]"""
        sof = b'\x7e'
        length = len(payload)
        # 帧内容部分 (用于计算CRC): CMD + LEN(2字节) + Payload
        frame_content = struct.pack('<BH', cmd, length) + payload
        crc = crc16_ccitt(frame_content)
        # 完整物理包
        full_packet = sof + frame_content + struct.pack('<H', crc)
        self.ser.write(full_packet)

    def recv_packet(self):
        """接收并解析响应包"""
        # 找帧头
        while True:
            byte = self.ser.read(1)
            if not byte: return None, None
            if byte == b'\x7e': break
        
        # 读头部 (CMD + LEN)
        hdr = self.ser.read(3)
        if len(hdr) < 3: return None, None
        cmd, length = struct.unpack('<BH', hdr)
        
        # 读负载
        payload = self.ser.read(length) if length > 0 else b''
        
        # 读校验
        crc_bytes = self.ser.read(2)
        if len(crc_bytes) < 2: return None, None
        rx_crc = struct.unpack('<H', crc_bytes)[0]
        
        # 校验 CRC
        frame_content = hdr + payload
        if crc16_ccitt(frame_content) != rx_crc:
            print("Error: CRC Checksum Mismatch!")
            return None, None
            
        return cmd, payload

    def sync(self):
        """循环发送 PING 直到主板响应"""
        print(f"[*] 正在尝试连接主板 (Syncing)...")
        self.ser.flushInput()
        while True:
            self.send_packet(CMD_PING)
            cmd, payload = self.recv_packet()
            if cmd == (CMD_PING | ACK_MASK):
                print(f"[+] 连接成功! 固件版本: v{payload[0]}.{payload[1]}")
                return True
            time.sleep(0.2)

    def get_info(self):
        """获取 Flash 布局信息"""
        self.send_packet(CMD_INFO)
        cmd, payload = self.recv_packet()
        if cmd == (CMD_INFO | ACK_MASK):
            addr, size = struct.unpack('<II', payload)
            print(f"[*] 目标地址: 0x{addr:08X}, 最大容量: {size/1024:.1f} KB")
            return addr, size
        return None, None

    def erase(self):
        """擦除应用区"""
        print("[*] 正在擦除 Flash...")
        self.send_packet(CMD_ERASE)
        cmd, payload = self.recv_packet()
        if cmd == (CMD_ERASE | ACK_MASK) and payload[0] == 0:
            print("[+] 擦除完成")
            return True
        print("[-] 擦除失败!")
        return False

    def write_firmware(self, start_addr, data):
        total_len = len(data)
        print(f"[*] 开始写入固件 ({total_len} 字节)...")
        
        # 错误代码映射表
        ERROR_MAP = {
            0x01: "HAL_ERROR (通用错误，通常是硬件拒绝写入)",
            0x02: "HAL_BUSY (Flash 忙，可能正在执行其他操作)",
            0x03: "HAL_TIMEOUT (操作超时)",
            0x10: "FLASH_WRPERR (写保护错误！请执行 st-flash erase)",
            0x11: "FLASH_PGERR (编程错误，地址可能未擦除或未对齐)",
        }

        for i in range(0, total_len, CHUNK_SIZE):
            chunk = data[i : i + CHUNK_SIZE]
            if len(chunk) % 2 != 0: chunk += b'\xFF'
            
            addr = start_addr + i
            payload = struct.pack('<IH', addr, len(chunk)) + chunk
            
            self.send_packet(CMD_WRITE, payload)
            cmd, res = self.recv_packet()
            print(f"\n[DEBUG] 收到响应: CMD={cmd}, Res={res}")
            
            if cmd != (CMD_WRITE | ACK_MASK) or res[0] != 0:
                err_code = res[0] if res else "Unknown"
                err_msg = ERROR_MAP.get(err_code, f"未知错误代码: {err_code}")
                print(f"\n\n[-] 写入失败 at 0x{addr:08X}")
                print(f"[-] 失败原因: {err_msg} {err_code}")
                
                # 额外诊断建议
                if err_code == 0x10:
                    print("[!] 建议：尝试运行 'st-flash erase' 后再试。")
                elif err_code == 0x01:
                    print("[!] 建议：检查 Bootloader 是否有自保护逻辑。")
                
                return False
            
            progress = (i + len(chunk)) / total_len * 100
            sys.stdout.write(f"\rProgress: [{progress:6.2f}%]")
            sys.stdout.flush()
            
        print("\n[+] 写入完成!")
        return True 

    def boot(self):
        """跳转到应用"""
        print("[*] 发送跳转指令...")
        self.send_packet(CMD_BOOT)
        cmd, payload = self.recv_packet()
        if cmd == (CMD_BOOT | ACK_MASK) and payload[0] == 0:
            print("[+] 主板正在启动应用...")
            return True
        print("[-] 跳转失败 (可能应用区无效)")
        return False

def main():
    parser = argparse.ArgumentParser(description='Hoverboard OTA Tool')
    parser.add_argument('file', nargs='?', help='Path to firmware .bin file')
    args = parser.parse_args()

    client = HoverBootloaderClient(SERIAL_PORT, BAUDRATE)

    # 1. 握手
    if not client.sync(): return

    if args.file:
        # --- 执行 OTA 逻辑 ---
        print(f"[*] 准备更新固件: {args.file}")
        with open(args.file, 'rb') as f:
            fw_data = f.read()

        # 获取信息并验证大小
        target_addr, max_size = client.get_info()
        if not target_addr or len(fw_data) > max_size:
            print("[-] 固件太大或无法获取 Flash 信息")
            return

        # 擦除 -> 写入 -> 跳转
        if client.erase():
            if client.write_firmware(target_addr, fw_data):
                client.boot()
    else:
        # --- 直接跳转逻辑 ---
        print("[*] 未提供固件，尝试直接进入应用...")
        client.boot()

    print("[*] 完成")

if __name__ == "__main__":
    main()