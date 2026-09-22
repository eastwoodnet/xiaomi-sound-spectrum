#!/bin/sh
# Background supervisor for OH2P. Native services always retain priority.
self_dir=$(CDPATH= cd "$(dirname "$0")" && pwd -P) || exit 1
self="$self_dir/service-oh2p.sh"
base=${self_dir%/*}
state_dir=/tmp/xiaomi-spectrum-oh2p-service
log_file=/tmp/xiaomi-spectrum-oh2p-service.log
umask 077

identity() {
    case "$1" in ''|*[!0-9]*) return 1;; esac
    [ "$1" -gt 1 ] || return 1
    awk '$3 != "Z" {print $22}' "/proc/$1/stat" 2>/dev/null
}
matches() {
    [ -r "$1" ] || return 1
    read saved_pid saved_ticks < "$1" || return 1
    case "$saved_ticks" in ''|*[!0-9]*) return 1;; esac
    live_ticks=$(identity "$saved_pid") || return 1
    [ -n "$live_ticks" ] && [ "$live_ticks" = "$saved_ticks" ]
}
record_pid() {
    pid_ticks=$(identity "$2") || return 1
    [ -n "$pid_ticks" ] || return 1
    printf '%s %s\n' "$2" "$pid_ticks" > "$1"
}
log() {
    # Rotate only between child runs; the launcher may hold the log open.
    if [ -f "$log_file" ] && [ "$(wc -c < "$log_file")" -gt 65536 ]; then
        tail -c 32768 "$log_file" > "$log_file.new"
        mv "$log_file.new" "$log_file"
    fi
    printf '%s %s\n' "$(date '+%Y-%m-%d %H:%M:%S')" "$*" >> "$log_file"
}
status_set() {
    printf '%s\n' "$*" > "$state_dir/status.new"
    mv "$state_dir/status.new" "$state_dir/status"
}
load_options() {
    mode=auto
    brightness=20
    extra=
    if [ -f "$base/options" ]; then
        read mode brightness extra < "$base/options" || return 1
    fi
    "$self_dir/led_music_oh2p" --check-mode "$mode" || return 1
    case "$brightness" in ''|*[!0-9]*) return 1;; esac
    [ -z "$extra" ] && [ "$brightness" -ge 1 ] && [ "$brightness" -le 100 ]
}
remove_state() {
    rm -f "$state_dir/owner" "$state_dir/child" "$state_dir/launch.pid" \
          "$state_dir/status" "$state_dir/status.new"
    rmdir "$state_dir" 2>/dev/null
}
stop_recorded_child() {
    if matches "$state_dir/child"; then
        kill "$saved_pid" 2>/dev/null || true
        count=0
        while matches "$state_dir/child"; do
            [ "$count" -lt 50 ] || return 1
            sleep 0.1
            count=$((count+1))
        done
    fi
    rm -f "$state_dir/child"
}
stop_service() {
    if matches "$state_dir/owner"; then
        kill "$saved_pid" 2>/dev/null || return 1
        count=0
        while matches "$state_dir/owner"; do
            [ "$count" -lt 100 ] || { echo 'Service did not stop; check the log' >&2; return 1; }
            sleep 0.1
            count=$((count+1))
        done
    elif [ -d "$state_dir" ] && [ ! -f "$state_dir/owner" ]; then
        echo 'Service is starting or its state is incomplete; retry later' >&2
        return 1
    fi
    if [ -d "$state_dir" ]; then
        stop_recorded_child || return 1
        remove_state || return 1
    fi
}
worker() {
    exec </dev/null >/dev/null 2>&1
    record_pid "$state_dir/owner" "$$" || exit 1
    child=
    finish() {
        trap - EXIT HUP INT TERM
        stop_recorded_child || { log 'Child cleanup failed; retaining state'; exit 1; }
        if [ -n "$child" ]; then wait "$child" 2>/dev/null || true; fi
        log 'Stopped'
        remove_state
    }
    trap finish EXIT
    trap 'exit 0' HUP INT TERM
    load_options || { log 'Invalid options'; exit 2; }
    log "Started mode=$mode brightness=$brightness"
    stable=0
    status_set waiting
    while :; do
        sh "$self_dir/run-oh2p.sh" --check "$mode" "$brightness" >/dev/null 2>&1
        ready_rc=$?
        if [ "$ready_rc" = 0 ] && [ ! -d /tmp/xiaomi-spectrum-oh2p.lock ]; then
            stable=$((stable+1))
        else
            stable=0
            status_set waiting
            if [ "$ready_rc" != 0 ] && [ "$ready_rc" != 4 ]; then
                status_set "error: preflight=$ready_rc"
                sleep 5
                continue
            fi
        fi
        if [ "$stable" -lt 3 ]; then sleep 1; continue; fi
        stable=0
        log 'Starting visualizer'
        sh "$self_dir/run-oh2p.sh" "$mode" "$brightness" 0 >> "$log_file" 2>&1 &
        child=$!
        record_pid "$state_dir/child" "$child" || true
        status_set active
        wait "$child"
        child_rc=$?
        child=
        rm -f "$state_dir/child"
        log "Visualizer exited: $child_rc"
        status_set waiting
        case "$child_rc" in
            0|4) sleep 1;;
            *) status_set "backoff: exit=$child_rc"; sleep 5;;
        esac
    done
}
start_service() {
    if matches "$state_dir/owner"; then
        echo "Already running: $saved_pid"
        return 0
    fi
    if [ -d "$state_dir" ]; then stop_service || return 1; fi
    load_options || { echo 'Invalid mode/brightness options' >&2; return 2; }
    sh "$self_dir/run-oh2p.sh" --check "$mode" "$brightness" >/dev/null 2>&1
    check_rc=$?
    case "$check_rc" in 0|4) ;; *) echo 'OH2P preflight failed' >&2; return "$check_rc";; esac
    mkdir "$state_dir" || return 1
    start-stop-daemon -S -b -m -p "$state_dir/launch.pid" -x /bin/sh -- "$self" _run
    if [ "$?" != 0 ]; then remove_state; return 1; fi
    count=0
    while [ "$count" -lt 30 ]; do
        if matches "$state_dir/owner"; then echo "Started: $saved_pid"; return 0; fi
        sleep 0.1
        count=$((count+1))
    done
    echo 'Startup did not complete; check service state and log' >&2
    return 1
}

case "${1:-status}" in
    start) start_service;;
    stop) stop_service;;
    restart) stop_service && start_service;;
    status)
        if matches "$state_dir/owner"; then
            printf 'running pid=%s state=' "$saved_pid"
            cat "$state_dir/status" 2>/dev/null || printf 'starting\n'
            if [ -f "$base/enabled" ]; then echo 'autostart=enabled'; else echo 'autostart=disabled'; fi
        else
            echo 'stopped'
            exit 3
        fi
        ;;
    enable)
        [ "$base" = /data/xiaomi-sound-spectrum ] || { echo 'Autostart requires a persistent installation' >&2; exit 2; }
        touch "$base/enabled" && start_service
        ;;
    disable) rm -f "$base/enabled"; stop_service;;
    _run) worker;;
    *) echo 'Usage: service-oh2p.sh start|stop|restart|status|enable|disable' >&2; exit 2;;
esac
