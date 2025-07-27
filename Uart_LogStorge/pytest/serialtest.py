import serial
import random
import string
import time

def generate_random_sequence(length):
    """生成指定长度的随机字符串"""
    return ''.join(random.choices(string.ascii_letters + string.digits, k=length))

def test_serial_port(port, baudrate, sequence_length, send_frequency):
    """测试串口功能"""
    try:
        # 打开串口
        ser = serial.Serial(port, baudrate, timeout=1)
        print(f"成功打开串口 {port}，波特率 {baudrate}")

        while True:
            # 生成随机序列
            sequence = generate_random_sequence(sequence_length)
            print(f"发送序列: {sequence}")

            # 发送序列
            ser.write(sequence.encode())

            # 等待一段时间，确保数据接收完整
            time.sleep(0.1)

            # 读取回显数据
            received_data = ser.read(ser.in_waiting).decode()
            print(f"接收到的数据: {received_data}")

            # 检查回显是否包含发送的序列
            if sequence in received_data:
                print("回显包含发送的序列，测试通过")
            else:
                print("回显不包含发送的序列，测试失败")

            # 按照指定频率等待
            time.sleep(1 / send_frequency)

    except serial.SerialException as e:
        print(f"串口错误: {e}")
    except Exception as e:
        print(f"发生错误: {e}")
    finally:
        if 'ser' in locals() and ser.is_open:
            ser.close()
            print("串口已关闭")

if __name__ == "__main__":
    # 配置参数
    serial_port = 'COM3'  # 根据实际情况修改串口名称，Linux 下可能是 '/dev/ttyUSB0'
    baud_rate = 115200    # 波特率
    sequence_length = 10  # 随机序列长度
    send_frequency = 1    # 发送频率（次/秒）

    test_serial_port(serial_port, baud_rate, sequence_length, send_frequency)