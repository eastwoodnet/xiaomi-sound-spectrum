#!/bin/sh
# ==============================================================================
# 小米 Sound (L06A) 智能声光律动守护服务 (Smart Music Companion Daemon)
#
# 功能说明:
#   1. 每 3 秒通过 ubus 检测音箱当前是否有音乐在播放 (DLNA / 语音点播 / 手机推送)。
#   2. 检测到放歌 (PLAYING) 时:
#      - 优雅停止官方 ledserver，释放 LED 控制权；
#      - 自动拉起原生 1024 点定点 FFT 音乐律动引擎 (led_music)。
#   3. 音乐暂停 / 停止后 (执行高雅谢幕仪式动画):
#      - 麦克风静音状态: 红色光芒由顶部双向逐对亮起延展，最终满圈常亮停在红光上，明确指示静音！
#      - 麦克风未静音状态: 柔和白光首先全亮，随后向顶部双向对称逐对收拢熄灭，最后完全黑屏待机！
#      - 动画过程中若切歌有新音乐注入，毫秒级无缝打断退场并继续律动！
#      - 动画结束后恢复官方 ledserver 原生服务。
# ==============================================================================

GUARD_PID_FILE="/tmp/led_guard.pid"
MUSIC_BIN="/data/led_music"
POLL_INTERVAL=3
LED_RGB="/sys/devices/i2c-0/0-003a/led_rgb"

# 写入自身 PID
echo $$ > "$GUARD_PID_FILE"

is_music_running() {
    killall -0 led_music 2>/dev/null
}

get_play_status() {
    # 状态码解析: 1 = PLAYING(播放中), 2 = PAUSED(暂停), 0 = STOPPED(停止)
    # 通过 ubus 高效查询，兼容转义与非转义的 JSON 字符串
    local raw st
    raw=$(ubus call mediaplayer player_get_play_status 2>/dev/null)
    st=$(echo "$raw" | grep -o 'status[^,}]*' | head -n 1 | tr -dc '0-9')
    if [ -z "$st" ] || [ "$st" = "0" ]; then
        raw=$(ubus call mediaplayer player_play_status 2>/dev/null)
        st=$(echo "$raw" | grep -o 'status[^,}]*' | head -n 1 | tr -dc '0-9')
    fi
    echo "${st:-0}"
}

start_music_visualizer() {
    if [ -f "$MUSIC_BIN" ]; then
        # 1. 停止官方 LED 交互服务并确保杀死残留
        /etc/init.d/led stop 2>/dev/null
        killall -9 ledserver 2>/dev/null
        # 2. 检查模式配置 (默认为 auto 每分钟自动轮换四种模式)
        local target_mode="auto"
        if [ -f /data/led_mode ]; then
            target_mode=$(cat /data/led_mode | tr -d ' \n\r')
        fi
        [ -z "$target_mode" ] && target_mode="auto"
        # 3. 启动原生 1024点 FFT 音乐律动
        start-stop-daemon -S -b -m -p /tmp/led_music.pid -x "$MUSIC_BIN" -- "$target_mode"
    fi
}

is_night_mode() {
    # 检测是否处于夜间时段 (22:00 ~ 06:00)，避免夜间动画过于耀眼
    local hm
    hm=$(date +%H%M 2>/dev/null)
    [ -z "$hm" ] && return 1
    if [ "$hm" -ge 2200 ] || [ "$hm" -lt 600 ]; then
        return 0
    fi
    return 1
}

play_exit_animation() {
    [ ! -f "$LED_RGB" ] && return 0

    # 1. 先停止音乐律动进程与 arecord，接管 LED 控制权
    killall -9 led_music arecord 2>/dev/null
    rm -f /tmp/led_music.pid

    # 2. 根据是否处于夜间模式自动选择柔和亮度
    local COLOR_RED="0x0000D0"
    local COLOR_WHITE="0x808080"
    if is_night_mode; then
        COLOR_RED="0x000045"    # 夜间模式：微暗暗红，柔和不刺眼
        COLOR_WHITE="0x252525"  # 夜间模式：极暗月光白，轻柔静谧
    fi

    if [ -f /tmp/mipns/mute ]; then
        # ==============================================================
        # 场景 A: 麦克风处于静音状态 —— 红色光芒双向延展逐颗亮起，停留在红光
        # ==============================================================
        echo "0 $COLOR_RED" > "$LED_RGB" 2>/dev/null
        sleep 0.25
        for step in 1 2 3 4 5 6 7 8; do
            if [ "$(get_play_status)" = "1" ]; then
                return 1   # 切歌快速打断，恢复播放
            fi
            l=$((0 + step))
            r=$((18 - step))
            echo "$l $COLOR_RED" > "$LED_RGB" 2>/dev/null
            echo "$r $COLOR_RED" > "$LED_RGB" 2>/dev/null
            sleep 0.2
        done
        echo "9 $COLOR_RED" > "$LED_RGB" 2>/dev/null
        sleep 0.8
    else
        # ==============================================================
        # 场景 B: 麦克风未静音 —— 白色柔光全亮提示律动结束，随后双向收拢完全熄灭
        # ==============================================================
        for i in $(seq 0 17); do
            echo "$i $COLOR_WHITE" > "$LED_RGB" 2>/dev/null
        done
        sleep 0.8

        echo "9 0" > "$LED_RGB" 2>/dev/null
        sleep 0.2
        for step in 1 2 3 4 5 6 7 8; do
            if [ "$(get_play_status)" = "1" ]; then
                return 1   # 切歌快速打断，恢复播放
            fi
            l=$((9 - step))
            r=$((9 + step))
            echo "$l 0" > "$LED_RGB" 2>/dev/null
            echo "$r 0" > "$LED_RGB" 2>/dev/null
            sleep 0.2
        done
        echo "0 0" > "$LED_RGB" 2>/dev/null
        sleep 0.5
    fi

    return 0
}

stop_music_visualizer() {
    # 1. 杀死律动程序与录音子进程
    killall -9 led_music arecord 2>/dev/null
    rm -f /tmp/led_music.pid
    
    # 2. 清理底层硬件灯珠残留 (物理全灭)
    if [ -f "$LED_RGB" ]; then
        I=0
        while [ $I -lt 18 ]; do
            echo "$I 0" > "$LED_RGB" 2>/dev/null
            I=$((I+1))
        done
    fi
    
    # 3. 恢复官方 ledserver 原生服务
    /etc/init.d/led start 2>/dev/null
    
    # 4. 等待 ledserver 注册进 ubus (最多等 1.5 秒)
    for t in 1 2 3 4 5; do
        if ubus list led >/dev/null 2>&1; then
            break
        fi
        sleep 0.3
    done
    
    # 5. 精确遵循系统默认规范:
    if [ -f /tmp/mipns/mute ]; then
        # 麦克风处于静音状态: 亮起官方原厂 7 号红色静音指示灯
        /bin/show_led 7 2>/dev/null
    else
        # 麦克风正常开启 (未静音): 严格遵循系统默认规范——灯带彻底关闭 (完全黑屏，0光污染)！
        ubus call led shut 2>/dev/null
        /bin/show_led c 2>/dev/null
    fi
}

cleanup() {
    stop_music_visualizer
    rm -f "$GUARD_PID_FILE"
    exit 0
}

trap cleanup INT TERM EXIT HUP

# 启动初始化: 检查当前状态
current_st=$(get_play_status)
if [ "$current_st" = "1" ]; then
    start_music_visualizer
else
    if is_music_running; then
        stop_music_visualizer
    else
        /etc/init.d/led start 2>/dev/null
        for t in 1 2 3 4 5; do
            if ubus list led >/dev/null 2>&1; then
                break
            fi
            sleep 0.3
        done
        if [ -f /tmp/mipns/mute ]; then
            /bin/show_led 7 2>/dev/null
        else
            ubus call led shut 2>/dev/null
            /bin/show_led c 2>/dev/null
        fi
    fi
fi

while true; do
    status=$(get_play_status)

    if [ "$status" = "1" ]; then
        # 正在播放音乐
        if ! is_music_running; then
            start_music_visualizer
        fi
    else
        # 音乐停止/暂停：如果律动正在跑，执行优雅谢幕动画
        if is_music_running; then
            play_exit_animation
            ret=$?
            if [ $ret -eq 1 ]; then
                # 动画期间检测到切歌恢复，重新拉起律动
                start_music_visualizer
            else
                # 动画正常谢幕完成，恢复官方服务
                stop_music_visualizer
            fi
        fi
    fi

    sleep $POLL_INTERVAL
done
