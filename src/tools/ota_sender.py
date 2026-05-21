#!/usr/bin/env python3
"""
WS63 智能小车局域网 OTA 固件推送工具

用法:
    python ota_sender.py <firmware.pkg> [device_ip]
    python ota_sender.py firmware.pkg                # 自动发现小车
    python ota_sender.py firmware.pkg 192.168.1.1   # 指定 IP

工作机制:
    小车在未连接控制器时会以 500ms 的频率向 UDP 8889 端口广播发现包
    (type=0xFF, mac[6], name[16])。本脚本监听该端口自动定位小车 IP。
"""

import signal
import socket
import struct
import sys
import time

g_target_ip = None  # 全局变量，供信号处理使用

def cancel_ota(ip: str):
    """发送 OTA 取消命令"""
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.settimeout(1.0)
    try:
        pkt = bytes([0x05, 0x02, 0x00, 0x00, 0x00])
        sock.sendto(pkt, (ip, UDP_PORT))
        print(f"[UDP] 已发送 OTA 取消命令到 {ip}")
    except Exception as e:
        print(f"[UDP] 发送取消命令失败: {e}")
    finally:
        sock.close()

def signal_handler(sig, frame):
    """Ctrl-C 信号处理：优雅取消 OTA"""
    global g_target_ip
    if g_target_ip:
        print("\n[INFO] 检测到中断，正在取消 OTA...")
        cancel_ota(g_target_ip)
    sys.exit(0)

signal.signal(signal.SIGINT, signal_handler)

UDP_PORT = 8888           # OTA 触发端口
UDP_BROADCAST_PORT = 8889 # 小车发现广播端口
TCP_PORT = 8890
CHUNK_SIZE = 32768

DISCOVERY_TYPE = 0xFF
DISCOVERY_PKT_SIZE = 1 + 6 + 16  # type + mac + name
DISCOVERY_TIMEOUT_S = 10.0


def discover_robot(timeout: float = DISCOVERY_TIMEOUT_S):
    """监听 UDP 广播，自动发现小车 IP

    返回 (ip, name, mac_str) 或 None
    """
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.settimeout(1.0)
    try:
        sock.bind(("", UDP_BROADCAST_PORT))
    except OSError as e:
        print(f"[Discovery] 绑定端口 {UDP_BROADCAST_PORT} 失败: {e}")
        sock.close()
        return None

    print(f"[Discovery] 监听 UDP :{UDP_BROADCAST_PORT}, 等待小车广播 (最长 {timeout:.0f}s)...")
    deadline = time.time() + timeout
    try:
        while time.time() < deadline:
            try:
                data, addr = sock.recvfrom(64)
            except socket.timeout:
                continue

            if len(data) < DISCOVERY_PKT_SIZE or data[0] != DISCOVERY_TYPE:
                continue

            mac = data[1:7]
            name_raw = data[7:7 + 16]
            name = name_raw.split(b"\x00", 1)[0].decode("utf-8", errors="replace")
            mac_str = ":".join(f"{b:02X}" for b in mac)

            print(f"[Discovery] 发现小车: name={name}, mac={mac_str}, ip={addr[0]}")
            return addr[0], name, mac_str
    finally:
        sock.close()

    print("[Discovery] 超时未发现小车")
    return None


def udp_trigger_ota(ip: str) -> bool:
    """通过 UDP 发送 OTA 启动命令，等待设备 ACK"""
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.settimeout(3.0)
    try:
        # 5 字节 UDP 包: type=0x05, cmd=0x01, motor1=0, motor2=0, ir_data=0
        pkt = bytes([0x05, 0x01, 0x00, 0x00, 0x00])
        sock.sendto(pkt, (ip, UDP_PORT))
        print(f"[UDP] 发送 OTA 触发命令到 {ip}:{UDP_PORT}")

        data, addr = sock.recvfrom(16)
        if len(data) >= 2 and data[0] == 0x05:
            if data[1] == 0x00:
                print("[UDP] 设备响应: OK，TCP 服务已启动")
                return True
            elif data[1] == 0x01:
                print("[UDP] 设备响应: Busy，当前正在进行 OTA")
                return False
            else:
                print(f"[UDP] 设备响应: 未知状态 (cmd={data[1]})")
                return False
        print("[UDP] 收到异常响应")
        return False
    except socket.timeout:
        print("[UDP] 等待响应超时")
        return False
    finally:
        sock.close()


def send_firmware(ip: str, fw_path: str) -> bool:
    """通过 TCP 发送固件包"""
    with open(fw_path, "rb") as f:
        fw_data = f.read()
    total_size = len(fw_data)
    print(f"[TCP] 固件大小: {total_size} bytes ({total_size / 1024:.1f} KB)")

    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.settimeout(60.0)
    try:
        print(f"[TCP] 连接 {ip}:{TCP_PORT} ...")
        sock.connect((ip, TCP_PORT))
        sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        print("[TCP] 已连接 (TCP_NODELAY=1)")

        # 1. 发送 8 字节 Header
        header = b"OTAx" + struct.pack(">I", total_size)
        sock.sendall(header)
        print("[TCP] 发送 Header (Magic + Size)")

        # 2. 等待 1 byte ACK
        ack = sock.recv(1)
        if not ack or ack[0] != 0x00:
            print(f"[TCP] Header ACK 错误: {ack.hex() if ack else 'None'}")
            return False
        print("[TCP] Header ACK OK")

        # 3. 流式发送固件（无逐块 ACK，固件侧边收边写）
        offset = 0
        start_time = time.time()
        while offset < total_size:
            chunk = fw_data[offset:offset + CHUNK_SIZE]
            sock.sendall(chunk)
            offset += len(chunk)
            pct = offset * 100 // total_size
            elapsed = time.time() - start_time
            speed = offset / elapsed / 1024 if elapsed > 0 else 0
            print(f"\r[TCP] 进度: {offset}/{total_size} bytes ({pct}%)  {speed:.1f} KB/s", end="", flush=True)

        print()  # 换行
        print("[TCP] 已写入本地发送缓冲，等待设备接收/校验/重启...")

        # 4. 等待最终 ACK（校验成功后设备会发 0x00，然后重启）
        sock.settimeout(60.0)
        try:
            final_ack = sock.recv(1)
            if final_ack and final_ack[0] == 0x00:
                print("[TCP] 校验通过，设备即将重启")
            else:
                print(f"[TCP] 校验结果未知: {final_ack.hex() if final_ack else 'None'}")
        except socket.timeout:
            # 设备可能已立即重启，socket 断开，这是正常的
            print("[TCP] 设备已断开（可能正在重启）")

        return True
    except Exception as e:
        print(f"[TCP] 传输异常: {e}")
        return False
    finally:
        sock.close()


def parse_args():
    """解析命令行参数

    支持两种形式（保持向后兼容）:
        python ota_sender.py firmware.pkg
        python ota_sender.py firmware.pkg 192.168.1.1
        python ota_sender.py 192.168.1.1 firmware.pkg   # 旧风格
    """
    args = sys.argv[1:]
    if not args:
        return None, "firmware.pkg"

    fw_path = None
    device_ip = None
    for a in args:
        # 简单判别：包含三个点且首段为数字 → IP
        parts = a.split(".")
        if len(parts) == 4 and all(p.isdigit() for p in parts):
            device_ip = a
        else:
            fw_path = a

    if fw_path is None:
        fw_path = "firmware.pkg"
    return device_ip, fw_path


def main():
    global g_target_ip
    device_ip, firmware_path = parse_args()

    print(f"=" * 50)
    print(f"WS63 OTA 固件推送工具")
    print(f"固件包:  {firmware_path}")
    print(f"设备 IP: {device_ip if device_ip else '<自动发现>'}")
    print(f"=" * 50)

    if device_ip is None:
        result = discover_robot()
        if result is None:
            print("[ERR] 未发现小车，请确认小车已开机并与本机处于同一局域网")
            sys.exit(1)
        device_ip = result[0]

    g_target_ip = device_ip  # 设置全局变量，供信号处理使用

    if not udp_trigger_ota(device_ip):
        sys.exit(1)

    # 给设备一点时间来创建 TCP 监听任务
    time.sleep(0.5)

    if not send_firmware(device_ip, firmware_path):
        sys.exit(1)

    print("[INFO] OTA 流程结束")


if __name__ == "__main__":
    main()
