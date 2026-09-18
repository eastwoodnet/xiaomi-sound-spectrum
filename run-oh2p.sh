#!/bin/sh
# OH2P 1.62.2 only. Keep factory services, the app preference and boot files intact.
# This foreground launcher exits on factory interaction; it does not auto-resume.
check_only=0
if [ "${1:-}" = --check ]; then check_only=1; shift; fi
mode=${1:-auto}
brightness=${2:-20}
duration=${3:-0}
case "$mode" in 1|2|auto) ;; *) echo 'Mode must be 1, 2 or auto' >&2; exit 2;; esac
case "$brightness" in ''|*[!0-9]*) exit 2;; esac
case "$duration" in ''|*[!0-9]*) exit 2;; esac
[ "$brightness" -ge 1 ] && [ "$brightness" -le 100 ] || exit 2
[ "$duration" -ge 0 ] && [ "$duration" -le 3600 ] || exit 2
[ "$#" -le 3 ] || exit 2

[ "$(micocfg_model 2>/dev/null)" = OH2P ] || { echo 'Requires OH2P' >&2; exit 3; }
grep -q 'Ver:1\.62\.2[[:space:]]*$' /etc/banner || { echo 'Only firmware 1.62.2 has been checked' >&2; exit 3; }
[ -w /sys/devices/i2c-2/2-0034/led_rgb ] || exit 3
[ -x /usr/bin/arecord ] || exit 3
cd "$(dirname "$0")" || exit 3
[ -x ./led_music_oh2p ] || { echo 'Build/download led_music_oh2p first' >&2; exit 3; }
. /usr/share/libubox/jshn.sh || exit 3
# Do not use set -u: factory jshn references optional unset variables.

snapshot() {
    reply=$(ubus -t 1 call mediaplayer get_playled_status '{}') || return 1
    json_load "$reply" || return 1
    json_get_var code code
    json_get_var info info
    [ "$code" = 0 ] && [ "$info" = 0 ] || return 1
    reply=$(ubus -t 1 call led status '{}') || return 1
    json_load "$reply" || return 1
    json_get_var state info
    [ "$state" = 'stored led ids: ; current id 0' ]
}

snapshot || { echo 'Turn off music lighting in the app and wait for factory dialogue to finish' >&2; exit 4; }
[ "$check_only" = 0 ] || exit 0
lock=/tmp/xiaomi-spectrum-oh2p.lock
mkdir "$lock" 2>/dev/null || { echo "Already running, or stale lock: $lock" >&2; exit 3; }
child=
started=0
cleanup() {
    trap - EXIT HUP INT TERM
    if [ -n "$child" ]; then
        kill "$child" 2>/dev/null || true
        wait "$child" 2>/dev/null || true
    fi
    # A poll is not an atomic LED lock. Never clear a detected factory display.
    if [ "$started" = 1 ] && snapshot; then
        i=0
        while [ "$i" -lt 12 ]; do
            printf '%s 0x000000\n' "$i" > /sys/devices/i2c-2/2-0034/led_rgb
            i=$((i+1))
        done
    fi
    rmdir "$lock" 2>/dev/null || true
}
trap cleanup EXIT
trap 'exit 0' HUP INT TERM
read uptime unused < /proc/uptime
deadline=$((${uptime%%.*} + duration))
./led_music_oh2p "$mode" "$brightness" &
child=$!
started=1
while kill -0 "$child" 2>/dev/null; do
    if ! snapshot; then
        echo 'Factory interaction or music lighting preference changed; yielding.'
        exit 0
    fi
    if [ "$duration" -ne 0 ]; then
        read uptime unused < /proc/uptime
        [ "${uptime%%.*}" -lt "$deadline" ] || exit 0
    fi
    sleep 0.1
done
wait "$child"
rc=$?
child=
exit "$rc"
