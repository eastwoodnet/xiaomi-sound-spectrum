#!/usr/bin/env python3
"""Install the OH2P visualizer using the host's OpenSSH client."""
import argparse
import hashlib
import re
import shlex
import shutil
import subprocess
import sys
from pathlib import Path


def run(command, **kwargs):
    return subprocess.run(command, check=True, **kwargs)


def files(directory):
    binary = directory / "led_music_oh2p"
    launcher = directory / "run-oh2p.sh"
    if not binary.is_file() or not launcher.is_file():
        raise ValueError("需要 led_music_oh2p 和 run-oh2p.sh；可先执行 bash build.sh oh2p")
    header = binary.read_bytes()[:20]
    if len(header) < 20 or header[:6] != b"\x7fELF\x02\x01" or header[18:20] != b"\xb7\x00":
        raise ValueError("led_music_oh2p 必须是 ARM64 ELF 程序")
    return binary, launcher


def preflight():
    # Check before creating a staging directory or uploading any files.
    return """set -eu
[ "$(micocfg_model 2>/dev/null)" = OH2P ] || { echo 'Requires OH2P' >&2; exit 3; }
grep -q 'Ver:1\\.62\\.2[[:space:]]*$' /etc/banner || { echo 'Requires firmware 1.62.2' >&2; exit 3; }
[ -w /sys/devices/i2c-2/2-0034/led_rgb ]
[ -x /usr/bin/arecord ]
[ -r /usr/share/libubox/jshn.sh ]
command -v ubus >/dev/null
command -v sha256sum >/dev/null
mktemp -d /tmp/oh2p-spectrum-upload.XXXXXX
"""


def install_command(stage, root, binary_hash, launcher_hash):
    destination = root + "/" + binary_hash[:12] + "-" + launcher_hash[:12]
    # All paths are fixed or validated locally, then shell-quoted as well.
    script = f"""set -eu
stage={shlex.quote(stage)}
root={shlex.quote(root)}
destination={shlex.quote(destination)}
lock=/tmp/xiaomi-spectrum-oh2p-install.lock
pending=
locked=0
cleanup() {{
    trap - EXIT HUP INT TERM
    rm -f "$stage/led_music_oh2p" "$stage/run-oh2p.sh"
    rmdir "$stage" 2>/dev/null || true
    if [ -n "$pending" ]; then
        rm -f "$pending/led_music_oh2p" "$pending/run-oh2p.sh"
        rmdir "$pending" 2>/dev/null || true
    fi
    if [ "$locked" = 1 ]; then rmdir "$lock" 2>/dev/null || true; fi
}}
trap cleanup EXIT
trap 'exit 1' HUP INT TERM
check_files() {{
    (cd "$1" && printf '%s\\n' '{binary_hash}  led_music_oh2p' '{launcher_hash}  run-oh2p.sh' | sha256sum -c -)
}}
check_files "$stage"
mkdir "$lock" || {{ echo 'Another install is active, or its install lock remains' >&2; exit 3; }}
locked=1
mkdir -p "$root"
if [ -e "$destination" ]; then
    check_files "$destination"
else
    pending=$(mktemp -d "$root/.install.XXXXXX")
    cp "$stage/led_music_oh2p" "$stage/run-oh2p.sh" "$pending/"
    chmod 755 "$pending/led_music_oh2p" "$pending/run-oh2p.sh"
    check_files "$pending"
    mv "$pending" "$destination"
    pending=
fi
printf 'Installed: %s\\n' "$destination"
"""
    return destination, script


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("host", help="音箱 IP 或 SSH 别名（以 root 连接）")
    parser.add_argument("--source-dir", type=Path, default=Path(__file__).resolve().parent,
                        help="包含 led_music_oh2p 和 run-oh2p.sh 的目录")
    parser.add_argument("--temporary", action="store_true", help="仅安装到 /tmp，重启后清除")
    parser.add_argument("--ssh-option", action="append", default=[], metavar="KEY=VALUE",
                        help="传给 ssh/scp 的 -o 选项，可重复指定")
    args = parser.parse_args(argv)
    if not re.fullmatch(r"[A-Za-z0-9_][A-Za-z0-9_.-]*|\[[A-Fa-f0-9:]+\]", args.host):
        parser.error("host 应为 IP、主机名、SSH 别名或方括号内的 IPv6 地址")
    try:
        binary, launcher = files(args.source_dir.resolve())
        if not shutil.which("ssh") or not shutil.which("scp"):
            raise ValueError("需要本机 OpenSSH 的 ssh 和 scp 命令")
        options = ["-o", "ConnectTimeout=10"]
        for option in args.ssh_option:
            options.extend(["-o", option])
        target = "root@" + args.host
        ssh = ["ssh", "-T", *options, target]
        print("检查 OH2P 型号、固件和接口……", flush=True)
        result = run([*ssh, preflight()], stdout=subprocess.PIPE, text=True)
        stage = result.stdout.strip()
        if not re.fullmatch(r"/tmp/oh2p-spectrum-upload\.[A-Za-z0-9]+", stage):
            raise ValueError("音箱返回了异常的临时目录，停止上传")
        binary_hash = hashlib.sha256(binary.read_bytes()).hexdigest()
        launcher_hash = hashlib.sha256(launcher.read_bytes()).hexdigest()
        root = "/tmp/xiaomi-sound-spectrum" if args.temporary else "/data/xiaomi-sound-spectrum"
        destination, command = install_command(stage, root, binary_hash, launcher_hash)
        print("上传 OH2P 程序与启动脚本……", flush=True)
        # -O uses SCP rather than SFTP, which factory Dropbear may not provide.
        run(["scp", "-O", *options, str(binary), str(launcher), target + ":" + stage + "/"])
        print("校验并安装……", flush=True)
        run([*ssh, command])
        print("\n安装完成。关闭 App 的音乐播放灯光效果后，登录音箱运行：")
        print("  sh " + destination + "/run-oh2p.sh auto 20 0")
        print("前台运行，Ctrl+C 停止；原厂交互时自动退出。再次运行同一命令即可启动。")
        print("旧版本保留；本次不修改开机脚本、不停止原厂服务、不自动启动。")
        return 0
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        print("部署失败：" + str(error), file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
