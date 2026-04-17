import serial
import struct
import time

# 请根据你的系统修改串口号，例如 'COM3' (Windows) 或 '/dev/ttyUSB0' (Linux)
ser = serial.Serial('/dev/ttyUSB0', 115200, timeout=0.1)

def send_cmd(steer, speed):
    start = 0xABCD
    checksum = (start ^ steer ^ speed) & 0xFFFF
    packet = struct.pack('<HhhH', start, steer, speed, checksum)
    ser.write(packet)

def receive_feedback():
    # SerialFeedback 结构体大小为 18 字节
    if ser.in_waiting >= 18:
        # 寻找起始字节 0xABCD (小端为 CD AB)
        while ser.in_waiting >= 18:
            first_byte = ser.read(1)
            if first_byte == b'\xcd':
                second_byte = ser.read(1)
                if second_byte == b'\xab':
                    # 读取剩余 16 字节
                    payload = ser.read(16)
                    # 解包: < (小端), 
                    # cmd1(h), cmd2(h), speedR(h), speedL(h), batV(h), boardTemp(h), cmdLed(H), checksum(H)
                    data = struct.unpack('<hhhhhhHH', payload)
                    
                    cmd1, cmd2, speedR, speedL, batV, temp, led, checksum = data
                    
                    # 验证校验和
                    calc_checksum = (0xABCD ^ cmd1 ^ cmd2 ^ speedR ^ speedL ^ batV ^ temp ^ led) & 0xFFFF
                    
                    if calc_checksum == checksum:
                        print(f"反馈数据: 电压={batV/100:.2f}V, 温度={temp/10:.1f}℃, R速度={speedR}, L速度={speedL}")
                        return
                    else:
                        print("校验和错误")
                else:
                    continue

try:
    print("正在发送前进指令 (速度 100) 并接收反馈... 按 Ctrl+C 停止")
    while True:
        send_cmd(0, 100)
        receive_feedback()
        time.sleep(0.05)
except KeyboardInterrupt:
    print("\n停止发送")
    send_cmd(0, 0)
    ser.close()
