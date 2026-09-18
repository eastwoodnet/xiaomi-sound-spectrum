#!/usr/bin/env python3
"""Install and manage the OH2P visualizer using the host's OpenSSH client."""
import argparse
import hashlib
import re
import shlex
import shutil
import subprocess
import sys
from pathlib import Path

PAYLOAD = ("led_music_oh2p", "run-oh2p.sh", "service-oh2p.sh")


def run(command, **kwargs):
    return subprocess.run(command, check=True, **kwargs)


def files(directory):
    paths = tuple(directory / name for name in PAYLOAD)
    if not all(path.is_file() for path in paths):
        raise ValueError("需要 led_music_oh2p、run-oh2p.sh 和 service-oh2p.sh")
    header = paths[0].read_bytes()[:20]
    if len(header) < 20 or header[:6] != b"\x7fELF\x02\x01" or header[18:20] != b"\xb7\x00":
        raise ValueError("led_music_oh2p 必须是 ARM64 ELF 程序")
    return paths


def preflight():
    return """set -eu
[ "$(micocfg_model 2>/dev/null)" = OH2P ] || { echo 'Requires OH2P' >&2; exit 3; }
grep -q 'Ver:1\\.62\\.2[[:space:]]*$' /etc/banner || { echo 'Requires firmware 1.62.2' >&2; exit 3; }
[ -w /sys/devices/i2c-2/2-0034/led_rgb ]
[ -x /usr/bin/arecord ]
[ -r /usr/share/libubox/jshn.sh ]
command -v ubus >/dev/null
command -v sha256sum >/dev/null
command -v start-stop-daemon >/dev/null
command -v awk >/dev/null
mktemp -d /tmp/oh2p-spectrum-upload.XXXXXX
"""


def boot_hook(root, init="/data/init.sh"):
    # Prefix preserves every original byte, including an early exit or exec.
    hook = (
        "#!/bin/sh\n# BEGIN xiaomi-sound-spectrum OH2P\n"
        f"if [ -f {shlex.quote(root + '/enabled')} ] && "
        f"[ -f {shlex.quote(root + '/service-oh2p.sh')} ]; then\n"
        f"    sh {shlex.quote(root + '/service-oh2p.sh')} start >/dev/null 2>&1\n"
        "fi\n# END xiaomi-sound-spectrum OH2P\n"
    )
    return f"""(
set -eu
init={shlex.quote(init)}
root={shlex.quote(root)}
[ ! -L "$init" ] || {{ echo 'Refusing to replace a symlinked init.sh' >&2; exit 3; }}
hook_file=$(mktemp "$root/.hook.XXXXXX")
init_new=
trap 'rm -f "$hook_file"; if [ -n "$init_new" ]; then rm -f "$init_new"; fi' EXIT
printf '%s' {shlex.quote(hook)} > "$hook_file"
if [ -f "$init" ] && grep -qF '# BEGIN xiaomi-sound-spectrum OH2P' "$init"; then
    head -n {hook.count(chr(10))} "$init" | cmp - "$hook_file" || {{
        echo 'Existing spectrum boot hook differs; refusing to overwrite it' >&2; exit 3;
    }}
else
    init_new=$(mktemp "$(dirname "$init")/.spectrum-init.XXXXXX")
    if [ -f "$init" ]; then
        before=$(sha256sum "$init" | awk '{{print $1}}')
        if [ ! -e "$root/init.sh.before-spectrum" ]; then cp -p "$init" "$root/init.sh.before-spectrum"; fi
        cp -p "$init" "$init_new"
        cat "$hook_file" "$init" > "$init_new"
        after=$(sha256sum "$init" | awk '{{print $1}}')
        [ "$before" = "$after" ] || {{ echo 'init.sh changed during installation' >&2; exit 3; }}
    else
        cat "$hook_file" > "$init_new"
        chmod 755 "$init_new"
    fi
    sh -n "$init_new"
    mv "$init_new" "$init"
    init_new=
fi
)
"""


def install_command(stage, root, hashes, *, temporary=False, mode=None,
                    brightness=None, autostart=True, start=True):
    manifest = "".join(f"{hashes[name]}  {name}\n" for name in PAYLOAD)
    version = hashlib.sha256(manifest.encode()).hexdigest()[:16]
    destination = root + "/" + version
    hook = boot_hook(root) if not temporary else ""
    control = "#!/bin/sh\nexec sh " + shlex.quote(destination + "/service-oh2p.sh") + ' "$@"\n'
    requested_mode = mode or ""
    requested_brightness = str(brightness) if brightness is not None else ""
    return destination, f"""set -eu
stage={shlex.quote(stage)}
root={shlex.quote(root)}
destination={shlex.quote(destination)}
lock=/tmp/xiaomi-spectrum-oh2p-install.lock
pending=
locked=0
cleanup() {{
    trap - EXIT HUP INT TERM
    for name in led_music_oh2p run-oh2p.sh service-oh2p.sh; do
        rm -f "$stage/$name"
        if [ -n "$pending" ]; then rm -f "$pending/$name"; fi
    done
    rmdir "$stage" 2>/dev/null || true
    if [ -n "$pending" ]; then rmdir "$pending" 2>/dev/null || true; fi
    if [ "$locked" = 1 ]; then
        rm -f "$lock/old-control" "$lock/old-options"
        rmdir "$lock" 2>/dev/null || true
    fi
}}
trap cleanup EXIT
trap 'exit 1' HUP INT TERM
check_files() {{ (cd "$1" && printf '%s' {shlex.quote(manifest)} | sha256sum -c -); }}
check_files "$stage"
mkdir "$lock" || {{ echo 'Another install is active, or its lock remains' >&2; exit 3; }}
locked=1
mkdir -p "$root"
if [ -e "$destination" ]; then
    check_files "$destination"
else
    pending=$(mktemp -d "$root/.install.XXXXXX")
    cp "$stage/led_music_oh2p" "$stage/run-oh2p.sh" "$stage/service-oh2p.sh" "$pending/"
    chmod 755 "$pending/led_music_oh2p" "$pending/run-oh2p.sh" "$pending/service-oh2p.sh"
    check_files "$pending"
    mv "$pending" "$destination"
    pending=
fi
old_mode=auto
old_brightness=20
extra=
if [ -f "$root/options" ]; then read old_mode old_brightness extra < "$root/options"; fi
mode={shlex.quote(requested_mode)}
brightness={shlex.quote(requested_brightness)}
mode=${{mode:-$old_mode}}
brightness=${{brightness:-$old_brightness}}
case "$mode" in 1|2|auto) ;; *) echo 'Invalid saved mode' >&2; exit 2;; esac
case "$brightness" in ''|*[!0-9]*) exit 2;; esac
[ -z "$extra" ] && [ "$brightness" -ge 1 ] && [ "$brightness" -le 100 ]
{hook}
was_running=0
was_enabled=0
if [ -f "$root/service-oh2p.sh" ]; then
    cp -p "$root/service-oh2p.sh" "$lock/old-control"
    if sh "$root/service-oh2p.sh" status >/dev/null 2>&1; then was_running=1; fi
fi
if [ -f "$root/options" ]; then cp -p "$root/options" "$lock/old-options"; fi
if [ -f "$root/enabled" ]; then was_enabled=1; fi
sh "$destination/service-oh2p.sh" stop
printf '%s %s\\n' "$mode" "$brightness" > "$root/options.new"
mv "$root/options.new" "$root/options"
printf '%s' {shlex.quote(control)} > "$root/service-oh2p.sh.new"
chmod 755 "$root/service-oh2p.sh.new"
mv "$root/service-oh2p.sh.new" "$root/service-oh2p.sh"
{"touch" if autostart and not temporary else "rm -f"} "$root/enabled"
if {"true" if start else "false"} && ! sh "$root/service-oh2p.sh" start; then
    sh "$root/service-oh2p.sh" stop || true
    if [ -f "$lock/old-control" ]; then mv "$lock/old-control" "$root/service-oh2p.sh"; else rm -f "$root/service-oh2p.sh"; fi
    if [ -f "$lock/old-options" ]; then mv "$lock/old-options" "$root/options"; else rm -f "$root/options"; fi
    if [ "$was_enabled" = 1 ]; then touch "$root/enabled"; else rm -f "$root/enabled"; fi
    if [ "$was_running" = 1 ]; then sh "$root/service-oh2p.sh" start || true; fi
    echo 'Startup failed; previous activation restored' >&2
    exit 1
fi
printf 'Installed: %s\\n' "$destination"
if {"true" if start else "false"}; then sh "$root/service-oh2p.sh" status; fi
"""


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("host", nargs="?", help="音箱 IP 或 SSH 别名；省略时交互输入（以 root 连接）")
    parser.add_argument("--source-dir", type=Path, default=Path(__file__).resolve().parent)
    parser.add_argument("--temporary", action="store_true", help="仅使用 /tmp，不设置开机自启")
    parser.add_argument("--mode", choices=("1", "2", "auto"), help="默认保留已有配置，首次为 auto")
    parser.add_argument("--brightness", type=int, choices=range(1, 101), metavar="1..100")
    parser.add_argument("--no-autostart", action="store_true", help="不启用开机自启")
    parser.add_argument("--no-start", action="store_true", help="安装后暂不启动后台服务")
    parser.add_argument("--ssh-option", action="append", default=[], metavar="KEY=VALUE")
    args = parser.parse_args(argv)
    if args.host is None:
        try:
            args.host = input("请输入音箱 IP 或 SSH 别名: ").strip()
        except (EOFError, KeyboardInterrupt):
            print("\n已取消部署。", file=sys.stderr)
            return 1
    if not re.fullmatch(r"[A-Za-z0-9_][A-Za-z0-9_.-]*|\[[A-Fa-f0-9:]+\]", args.host):
        parser.error("host 应为 IP、主机名、SSH 别名或方括号内的 IPv6 地址")
    try:
        paths = files(args.source_dir.resolve())
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
        hashes = {path.name: hashlib.sha256(path.read_bytes()).hexdigest() for path in paths}
        root = "/tmp/xiaomi-sound-spectrum" if args.temporary else "/data/xiaomi-sound-spectrum"
        destination, command = install_command(
            stage, root, hashes, temporary=args.temporary, mode=args.mode,
            brightness=args.brightness, autostart=not args.no_autostart, start=not args.no_start)
        print("上传程序和服务脚本……", flush=True)
        run(["scp", "-O", *options, *map(str, paths), target + ":" + stage + "/"])
        print("校验并部署后台服务……", flush=True)
        run([*ssh, command])
        if args.no_start:
            print("\n部署完成，后台服务尚未启动。")
        else:
            print("\n部署完成。App 的音乐播放灯光效果关闭后，后台服务随音乐显示彩色频谱。")
            print("原厂对话期间自动让出灯带，结束后恢复；可以关闭 SSH 连接。")
        print("管理命令：sh " + root + "/service-oh2p.sh start|stop|restart|status|enable|disable")
        print("日志：/tmp/xiaomi-spectrum-oh2p-service.log")
        print("安装目录：" + destination)
        return 0
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        print("部署失败：" + str(error), file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
