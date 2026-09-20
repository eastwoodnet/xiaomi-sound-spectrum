#!/usr/bin/env python3
# ==============================================================================
# 小米 Sound (L06A) 音乐律动程序一键部署脚本
# 使用方法:
#   python3 deploy.py <SPEAKER_IP> <SSH_PASSWORD>
# 例如:
#   python3 deploy.py 192.168.1.100 my_password
# ==============================================================================

import os
import sys
import time
import socket
import getpass
from hashlib import sha1

try:
    import paramiko
    import paramiko.kex_group14
    from cryptography.hazmat.primitives import hashes
except ImportError:
    print("错误: 缺少依赖，请先执行: pip install paramiko cryptography")
    sys.exit(1)

# 注册旧版 dropbear 兼容的 KEX 和 HostKey 算法
class KexGroup14SHA1(paramiko.kex_group14.KexGroup14SHA256):
    name = "diffie-hellman-group14-sha1"
    hash_algo = sha1

paramiko.Transport._kex_info["diffie-hellman-group14-sha1"] = KexGroup14SHA1
paramiko.RSAKey.HASHES["ssh-rsa"] = hashes.SHA1
paramiko.Transport._key_info["ssh-rsa"] = paramiko.RSAKey

def get_credentials():
    host = os.environ.get("SPEAKER_IP")
    password = os.environ.get("SPEAKER_PASSWORD")

    if len(sys.argv) >= 3:
        host = sys.argv[1]
        password = sys.argv[2]
    elif len(sys.argv) == 2:
        host = sys.argv[1]

    if not host:
        host = input("请输入音箱 IP 地址 (例如 192.168.1.100): ").strip()
    if not password:
        password = getpass.getpass("请输入音箱 SSH root 密码: ").strip()

    return host, password

def deploy():
    host, password = get_credentials()
    user = "root"
    
    bin_path = os.path.join(os.path.dirname(__file__), "led_music")
    build_script = os.path.join(os.path.dirname(__file__), "build.sh")

    if not os.path.exists(bin_path):
        print(f"[*] 未检测到编译完成的二进制文件: {bin_path}")
        if os.path.exists(build_script):
            print("[*] 正在尝试自动调用 ./build.sh 进行交叉编译 ...")
            ret = os.system(f"bash '{build_script}'")
            if ret != 0 or not os.path.exists(bin_path):
                print("[-] 自动编译失败！请先在宿主机安装 clang 与 lld (LLVM)，并执行 ./build.sh 编译生成 led_music。")
                sys.exit(1)
        else:
            print("[-] 未找到 build.sh，请先编译生成 led_music 原生二进制文件后再部署。")
            sys.exit(1)

    print(f"[*] 正在连接音箱 {host}:22 ...")
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.settimeout(15)
    sock.connect((host, 22))

    transport = paramiko.Transport(sock)
    transport._preferred_kex = ("diffie-hellman-group14-sha1", "curve25519-sha256@libssh.org")
    transport._preferred_keys = ("ssh-rsa", "ssh-ed25519")
    transport._strict_kex = False
    transport.disabled_algorithms = {"signatures": ["rsa-sha2-512", "rsa-sha2-256"]}

    try:
        transport.connect(username=user, password=password)
        print("[+] SSH 认证成功！")

        # 停止所有旧服务与残留进程
        chan = transport.open_session()
        chan.exec_command("killall -9 led_guard.sh led_music music_smooth.sh arecord 2>/dev/null")
        time.sleep(1)
        chan.close()

        print("[*] 正在上传 aarch64 原生高帧率程序 led_music 到音箱 /data/led_music ...")
        with open(bin_path, "rb") as f:
            content = f.read()

        chan = transport.open_session()
        chan.exec_command("cat > /data/led_music && chmod +x /data/led_music")
        chan.sendall(content)
        chan.shutdown_write()
        time.sleep(1)
        chan.close()
        print("[+] 原生二进制上传完毕并赋予可执行权限。")

        guard_path = os.path.join(os.path.dirname(__file__), "led_guard.sh")
        if os.path.exists(guard_path):
            print("[*] 正在上传智能声光律动守护脚本 led_guard.sh 到音箱 /data/led_guard.sh ...")
            with open(guard_path, "rb") as f:
                guard_content = f.read()
            chan = transport.open_session()
            chan.exec_command("cat > /data/led_guard.sh && chmod +x /data/led_guard.sh")
            chan.sendall(guard_content)
            chan.shutdown_write()
            time.sleep(1)
            chan.close()
            print("[+] 守护脚本上传完毕并赋予可执行权限。")

        print("[*] 正在配置开机自启 /data/init.sh (断电重启自动保持)...")
        chan = transport.open_session()
        chan.exec_command("""cat << 'EOF' > /data/init.sh
#!/bin/sh
# 1. 默认先恢复官方 led 交互服务 (唤醒/音量/通知灯效)
/etc/init.d/led start 2>/dev/null

# 2. 终止残留守护进程
killall -9 led_guard.sh 2>/dev/null

# 3. 启动智能声光律动后台守护服务
if [ -f /data/led_guard.sh ]; then
    /data/led_guard.sh >/dev/null 2>&1 &
fi
EOF
chmod +x /data/init.sh
""")
        time.sleep(1)
        chan.close()
        print("[+] 开机持久化自启配置完成！")

        print("[*] 正在启动智能声光律动守护服务 (3秒轮询检测 / 放歌自动律动 / 闲置还原官方)...")
        chan = transport.open_session()
        chan.exec_command("/data/led_guard.sh >/dev/null 2>&1 &")
        time.sleep(2)
        chan.close()

        # 检查进程状态
        chan = transport.open_session()
        chan.exec_command("ps | grep -E 'led_guard|ledserver|led_music'; ubus call mediaplayer player_get_play_status 2>/dev/null")
        time.sleep(1)
        out = b""
        while chan.recv_ready():
            out += chan.recv(65535)
        print("\n[当前运行进程与状态]:")
        print(out.decode().strip())
        chan.close()

        print("\n" + "="*60)
        print("🎉 恭喜！小米 Sound 智能动态声光律动系统 (v1.0-beta2) 部署成功！")
        print("• 智能守护: 每 3 秒自动通过 ubus 监测音乐播放状态")
        print("• 音乐播放: 自动切入原生 1024点 FFT 震撼音乐律动")
        print("• 音乐停止/闲置: 自动切回官方 ledserver，100% 恢复呼唤小爱光环与音量交互")
        print("• 麦克风保护: 闲置时绝不抢占麦克风，说话走动绝不误闪乱动")
        print("• 持久自启: 开机自启 /data/init.sh 已永久生效，断电重启不丢失")
        print("="*60)

    finally:
        transport.close()

if __name__ == "__main__":
    deploy()
