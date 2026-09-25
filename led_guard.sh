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
#   4. Home Assistant MQTT 远端控制与自动发现 (MQTT Discovery):
#      - 开机自启后主动向 HA 发送 MQTT Discovery 注册报文 (自动生成实体，免配 YAML)；
#      - 实时订阅 xiaomi_sound/led/set 与 xiaomi_sound/led/power/set 控制指令；
#      - 毫秒级热切换四大模式、自动轮换与关闭模式；
#      - 自动轮换或状态变化时，双向回传状态，HA 界面同步更新；
#      - 支持 LWT 遗嘱消息，音箱离线或断电时 HA 实体自动呈现不可用。
# ==============================================================================

GUARD_PID_FILE="/tmp/led_guard.pid"
MUSIC_BIN="/data/led_music"
POLL_INTERVAL=3
LED_RGB="/sys/devices/i2c-0/0-003a/led_rgb"

# ---------------- MQTT 远端控制配置与辅助函数 ----------------
MQTT_CONF="/data/mqtt.conf"
MQTT_HOST="192.168.1.1"
MQTT_PORT="1883"
MQTT_USER=""
MQTT_PASS=""
MQTT_ENABLED="1"

if [ -f "$MQTT_CONF" ]; then
    . "$MQTT_CONF"
fi

mqtt_pub() {
    [ "$MQTT_ENABLED" != "1" ] && return 0
    if [ -n "$MQTT_USER" ] && [ -n "$MQTT_PASS" ]; then
        mosquitto_pub -h "$MQTT_HOST" -p "$MQTT_PORT" -u "$MQTT_USER" -P "$MQTT_PASS" "$@" 2>/dev/null
    elif [ -n "$MQTT_USER" ]; then
        mosquitto_pub -h "$MQTT_HOST" -p "$MQTT_PORT" -u "$MQTT_USER" "$@" 2>/dev/null
    else
        mosquitto_pub -h "$MQTT_HOST" -p "$MQTT_PORT" "$@" 2>/dev/null
    fi
}

mqtt_sub() {
    [ "$MQTT_ENABLED" != "1" ] && return 0
    if [ -n "$MQTT_USER" ] && [ -n "$MQTT_PASS" ]; then
        mosquitto_sub -h "$MQTT_HOST" -p "$MQTT_PORT" -u "$MQTT_USER" -P "$MQTT_PASS" "$@" 2>/dev/null
    elif [ -n "$MQTT_USER" ]; then
        mosquitto_sub -h "$MQTT_HOST" -p "$MQTT_PORT" -u "$MQTT_USER" "$@" 2>/dev/null
    else
        mosquitto_sub -h "$MQTT_HOST" -p "$MQTT_PORT" "$@" 2>/dev/null
    fi
}

report_state() {
    local mode_name="$1"
    local pwr="$2"
    local cur_active="$3"
    [ -n "$mode_name" ] && mqtt_pub -t "xiaomi_sound/led/state" -m "$mode_name" -r
    [ -n "$pwr" ] && mqtt_pub -t "xiaomi_sound/led/power/state" -m "$pwr" -r
    [ -n "$cur_active" ] && mqtt_pub -t "xiaomi_sound/led/current_mode" -m "$cur_active" -r
}

send_ha_discovery() {
    [ "$MQTT_ENABLED" != "1" ] && return 0
    
    # 1. 注册 Select 实体: 律动模式选择器
    local disc_select="homeassistant/select/xiaomi_sound_l06a/led_mode/config"
    local payload_select='{"name":"声光律动模式","unique_id":"xiaomi_sound_l06a_led_mode","command_topic":"xiaomi_sound/led/set","state_topic":"xiaomi_sound/led/state","availability_topic":"xiaomi_sound/led/availability","payload_available":"online","payload_not_available":"offline","icon":"mdi:music-note-outline","options":["自动轮换","模式 1: 天使之翼","模式 2: 低音怒火","模式 3: 流动熔岩","模式 4: 全频律动","模式 5: 极速光轮","关闭律动 (恢复官方)"],"device":{"identifiers":["xiaomi_sound_l06a"],"name":"Xiaomi Sound Light","model":"L06A","manufacturer":"Xiaomi","sw_version":"v1.1-release"}}'
    mqtt_pub -t "$disc_select" -m "$payload_select" -r

    # 2. 注册 Switch 实体: 律动总开关
    local disc_switch="homeassistant/switch/xiaomi_sound_l06a/visualizer/config"
    local payload_switch='{"name":"声光律动总开关","unique_id":"xiaomi_sound_l06a_visualizer_switch","command_topic":"xiaomi_sound/led/power/set","state_topic":"xiaomi_sound/led/power/state","availability_topic":"xiaomi_sound/led/availability","payload_available":"online","payload_not_available":"offline","payload_on":"ON","payload_off":"OFF","icon":"mdi:speaker-wireless","device":{"identifiers":["xiaomi_sound_l06a"],"name":"Xiaomi Sound Light","model":"L06A","manufacturer":"Xiaomi","sw_version":"v1.1-release"}}'
    mqtt_pub -t "$disc_switch" -m "$payload_switch" -r

    # 3. 注册 Sensor 实体: 当前运行律动 (实时显示实际运行模式或待机状态)
    local disc_sensor="homeassistant/sensor/xiaomi_sound_l06a/current_mode/config"
    local payload_sensor='{"name":"当前运行律动","unique_id":"xiaomi_sound_l06a_current_mode","state_topic":"xiaomi_sound/led/current_mode","availability_topic":"xiaomi_sound/led/availability","payload_available":"online","payload_not_available":"offline","icon":"mdi:waveform","device":{"identifiers":["xiaomi_sound_l06a"],"name":"Xiaomi Sound Light","model":"L06A","manufacturer":"Xiaomi","sw_version":"v1.1-release"}}'
    mqtt_pub -t "$disc_sensor" -m "$payload_sensor" -r

    # 4. 注册 Select 实体: 声光主题风格 (Theme)
    local disc_theme="homeassistant/select/xiaomi_sound_l06a/led_theme/config"
    local payload_theme='{"name":"声光主题风格","unique_id":"xiaomi_sound_l06a_led_theme","command_topic":"xiaomi_sound/led/theme/set","state_topic":"xiaomi_sound/led/theme/state","availability_topic":"xiaomi_sound/led/availability","payload_available":"online","payload_not_available":"offline","icon":"mdi:palette-outline","options":["🌈 经典彩虹","🌆 赛博朋克","🌊 深海冰蓝","🔥 炽热烈焰","🌌 极光秘境","🎨 自定义调色"],"device":{"identifiers":["xiaomi_sound_l06a"],"name":"Xiaomi Sound Light","model":"L06A","manufacturer":"Xiaomi","sw_version":"v1.1-release"}}'
    mqtt_pub -t "$disc_theme" -m "$payload_theme" -r

    # 5. 初始上线通知 (Retained)
    mqtt_pub -t "xiaomi_sound/led/availability" -m "online" -r
}

get_theme_name() {
    local t="rainbow"
    if [ -f "/data/palettes.conf" ]; then
        t=$(grep -E '^[[:space:]]*THEME[[:space:]]*=' /data/palettes.conf 2>/dev/null | cut -d'=' -f2 | tr -d ' \t\r\n')
    fi
    case "$t" in
        "cyberpunk") echo "🌆 赛博朋克" ;;
        "ocean")     echo "🌊 深海冰蓝" ;;
        "fire")      echo "🔥 炽热烈焰" ;;
        "aurora")    echo "🌌 极光秘境" ;;
        "custom")    echo "🎨 自定义调色" ;;
        *)           echo "🌈 经典彩虹" ;;
    esac
}

handle_theme_command() {
    local cmd="$1"
    local t_val=""
    local t_name=""
    case "$cmd" in
        *"赛博朋克"*|"cyberpunk")
            t_val="cyberpunk"
            t_name="🌆 赛博朋克"
            ;;
        *"深海冰蓝"*|"ocean")
            t_val="ocean"
            t_name="🌊 深海冰蓝"
            ;;
        *"炽热烈焰"*|"fire")
            t_val="fire"
            t_name="🔥 炽热烈焰"
            ;;
        *"极光秘境"*|"aurora")
            t_val="aurora"
            t_name="🌌 极光秘境"
            ;;
        *"自定义"*|"custom")
            t_val="custom"
            t_name="🎨 自定义调色"
            ;;
        *"经典彩虹"*|"rainbow"|*)
            t_val="rainbow"
            t_name="🌈 经典彩虹"
            ;;
    esac

    if [ -f "/data/palettes.conf" ]; then
        if grep -q -E '^[[:space:]]*THEME[[:space:]]*=' /data/palettes.conf; then
            sed -i "s/^[[:space:]]*THEME[[:space:]]*=.*/THEME = $t_val/" /data/palettes.conf
        else
            echo "THEME = $t_val" >> /data/palettes.conf
        fi
    else
        echo "THEME = $t_val" > /data/palettes.conf
    fi

    touch /tmp/reload_palette
    mqtt_pub -t "xiaomi_sound/led/theme/state" -m "$t_name" -r
}

switch_mode_action() {
    local target_mode="$1"
    if is_music_running; then
        killall -9 led_music 2>/dev/null
        start-stop-daemon -S -b -m -p /tmp/led_music.pid -x "$MUSIC_BIN" -- "$target_mode"
    elif [ "$(get_play_status)" = "1" ]; then
        start_music_visualizer
    fi
}

handle_mode_command() {
    local cmd="$1"
    case "$cmd" in
        "1"|*"模式 1"*|*"天使之翼"*)
            echo "1" > /data/led_mode
            switch_mode_action "1"
            report_state "模式 1: 天使之翼" "ON" "模式 1: 天使之翼"
            ;;
        "2"|*"模式 2"*|*"低音怒火"*)
            echo "2" > /data/led_mode
            switch_mode_action "2"
            report_state "模式 2: 低音怒火" "ON" "模式 2: 低音怒火"
            ;;
        "3"|*"模式 3"*|*"流动熔岩"*|*"彩虹熔岩"*)
            echo "3" > /data/led_mode
            switch_mode_action "3"
            report_state "模式 3: 流动熔岩" "ON" "模式 3: 流动熔岩"
            ;;
        "4"|*"模式 4"*)
            echo "4" > /data/led_mode
            switch_mode_action "4"
            report_state "模式 4: 全频律动" "ON" "模式 4: 全频律动"
            ;;
        "5"|*"模式 5"*|*"极速光轮"*)
            echo "5" > /data/led_mode
            switch_mode_action "5"
            report_state "模式 5: 极速光轮" "ON" "模式 5: 极速光轮"
            ;;
        "6"|*"模式 6"*|*"警灯"*)
            echo "6" > /data/led_mode
            switch_mode_action "6"
            report_state "" "ON" "彩蛋模式: 警灯风暴"
            ;;
        "auto"|*"自动轮换"*)
            rm -f /data/led_mode
            switch_mode_action "auto"
            report_state "自动轮换" "ON" "自动轮换中..."
            ;;
        "off"|*"关闭"*)
            echo "off" > /data/led_mode
            stop_music_visualizer
            report_state "关闭律动 (恢复官方)" "OFF" "已关闭"
            ;;
    esac
}

mqtt_worker() {
    while true; do
        # 尝试发送上线与 Discovery 注册信息
        if send_ha_discovery; then
            # 上报当前初始状态
            cur_name="自动轮换"
            pwr_state="ON"
            cur_active="待机 (官方交互)"
            if [ -f /data/led_mode ]; then
                cur_val=$(cat /data/led_mode 2>/dev/null | tr -d ' \n\r')
                case "$cur_val" in
                    1) cur_name="模式 1: 天使之翼" ;;
                    2) cur_name="模式 2: 低音怒火" ;;
                    3) cur_name="模式 3: 流动熔岩" ;;
                    4) cur_name="模式 4: 全频律动" ;;
                    5) cur_name="模式 5: 极速光轮" ;;
                    6) cur_name=""; cur_active="彩蛋模式: 警灯风暴" ;;
                    off) cur_name="关闭律动 (恢复官方)"; pwr_state="OFF"; cur_active="已关闭" ;;
                esac
            fi
            if is_music_running; then
                cur_active="$cur_name"
            fi
            report_state "$cur_name" "$pwr_state" "$cur_active"
            cur_theme=$(get_theme_name)
            mqtt_pub -t "xiaomi_sound/led/theme/state" -m "$cur_theme" -r

            # 阻塞订阅控制指令，配置 LWT 离线遗嘱
            mqtt_sub --will-topic "xiaomi_sound/led/availability" \
                     --will-payload "offline" --will-retain \
                     -t "xiaomi_sound/led/set" \
                     -t "xiaomi_sound/led/power/set" \
                     -t "xiaomi_sound/led/theme/set" -v | while read -r line; do
                topic=$(echo "$line" | cut -d' ' -f1)
                payload=$(echo "$line" | cut -d' ' -f2-)
                if [ "$topic" = "xiaomi_sound/led/power/set" ]; then
                    if [ "$payload" = "OFF" ] || [ "$payload" = "off" ]; then
                        handle_mode_command "off"
                    elif [ "$payload" = "ON" ] || [ "$payload" = "on" ]; then
                        [ -f /data/led_mode ] && [ "$(cat /data/led_mode 2>/dev/null)" = "off" ] && rm -f /data/led_mode
                        handle_mode_command "auto"
                    fi
                elif [ "$topic" = "xiaomi_sound/led/set" ]; then
                    handle_mode_command "$payload"
                elif [ "$topic" = "xiaomi_sound/led/theme/set" ]; then
                    handle_theme_command "$payload"
                fi
            done
        fi
        sleep 5
    done
}

start_mqtt_daemon() {
    [ "$MQTT_ENABLED" != "1" ] && return 0
    mqtt_worker &
    MQTT_PID=$!
}

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
    # 如果用户通过 MQTT 设置了关闭律动 (off)，则不接管 LED
    if [ -f /data/led_mode ] && [ "$(cat /data/led_mode 2>/dev/null | tr -d ' \n\r')" = "off" ]; then
        return 0
    fi
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
    else
        # ==============================================================
        # 场景 B: 麦克风未静音 —— 白光全亮后向顶部双向收拢熄灭
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
    [ -n "$MQTT_PID" ] && kill -9 "$MQTT_PID" 2>/dev/null
    killall -9 mosquitto_sub 2>/dev/null
    mqtt_pub -t "xiaomi_sound/led/availability" -m "offline" -r
    stop_music_visualizer
    rm -f "$GUARD_PID_FILE"
    exit 0
}

trap cleanup INT TERM EXIT HUP

# 启动初始化: 拉起 MQTT 守护后台
start_mqtt_daemon

# 检查当前播放状态
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

last_vmode=""

while true; do
    status=$(get_play_status)

    if [ "$status" = "1" ]; then
        # 正在播放音乐
        if ! is_music_running; then
            start_music_visualizer
        fi

        # 监测自动轮换时的模式切变，向 HA 实时同步当前子模式
        if [ -f /tmp/visualizer_mode ]; then
            vmode_now=$(cat /tmp/visualizer_mode 2>/dev/null | head -n 1)
            if [ -n "$vmode_now" ] && [ "$vmode_now" != "$last_vmode" ]; then
                last_vmode="$vmode_now"
                cur_disp=""
                case "$vmode_now" in
                    *"模式 1"*|*"天使之翼"*) cur_disp="模式 1: 天使之翼" ;;
                    *"模式 2"*|*"低音怒火"*) cur_disp="模式 2: 低音怒火" ;;
                    *"模式 3"*|*"流动熔岩"*|*"彩虹熔岩"*) cur_disp="模式 3: 流动熔岩" ;;
                    *"模式 4"*) cur_disp="模式 4: 全频律动" ;;
                    *"模式 5"*|*"极速光轮"*) cur_disp="模式 5: 极速光轮" ;;
                    *"模式 6"*|*"警灯"*) cur_disp="彩蛋模式: 警灯风暴" ;;
                    *) cur_disp="$vmode_now" ;;
                esac
                [ -n "$cur_disp" ] && mqtt_pub -t "xiaomi_sound/led/current_mode" -m "$cur_disp" -r
            fi
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
                if [ -f /data/led_mode ] && [ "$(cat /data/led_mode 2>/dev/null)" = "off" ]; then
                    mqtt_pub -t "xiaomi_sound/led/current_mode" -m "已关闭" -r
                else
                    mqtt_pub -t "xiaomi_sound/led/current_mode" -m "待机 (官方交互)" -r
                fi
                last_vmode=""
            fi
        fi
    fi

    sleep $POLL_INTERVAL
done
