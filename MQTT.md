# 小米 Sound (L06A) Home Assistant MQTT 远端控制设计方案

## 1. 架构概述与网络拓扑

本方案旨在利用小米 Sound (L06A) 内部原生的 MQTT 客户端工具，通过局域网无缝对接主路由的 MQTT Broker，实现 **Home Assistant 零配置自动发现（MQTT Discovery）**、**双向实时状态同步** 与 **四大声光律动模式的毫秒级远程切换**。

```
 ┌─────────────────────────────────────────────────────────────────────────────┐
 │                            局域网 (192.168.1.0/24)                          │
 │                                                                             │
 │  ┌──────────────────────────┐             ┌──────────────────────────────┐  │
 │  │ Home Assistant (k3s容器) │             │     主路由 MQTT Broker       │  │
 │  │                          │  MQTT Pub   │      (192.168.1.1:1883)      │  │
 │  │ 自动发现实体:            │ ──────────> │                              │  │
 │  │ select.xiaomi_sound_led  │             │  • 统一消息路由中心          │  │
 │  │                          │ <────────── │  • 支持局域网免密/密码访问   │  │
 │  └──────────────────────────┘  MQTT Sub   └──────────────────────────────┘  │
 │                                                          ▲                  │
 │                                                 MQTT Sub │ MQTT Pub         │
 │                                                 & LWT    │ & Discovery      │
 │                                                          ▼                  │
 │                                           ┌──────────────────────────────┐  │
 │                                           │     小米 Sound 音箱 (L06A)   │  │
 │                                           │       (192.168.1.6)          │  │
 │                                           │                              │  │
 │                                           │  • /usr/bin/mosquitto_sub    │  │
 │                                           │  • /usr/bin/mosquitto_pub    │  │
 │                                           │  • /data/led_guard.sh 守护   │  │
 │                                           │  • /data/led_music 律动引擎  │  │
 │                                           └──────────────────────────────┘  │
 └─────────────────────────────────────────────────────────────────────────────┘
```

---

## 2. 核心设计原则

1. **绝对隔离原生服务（保护蓝牙 Mesh）**：
   - 音箱自带的 Mosquitto Broker 严格监听在 `127.0.0.1:1883`，仅供小爱内部的 `mibt_mesh_proxy`（蓝牙 Mesh 网关）等进程通信。
   - 远端控制方案**绝不修改音箱内部的 `mosquitto.conf`，也绝不占用或重启本地 1883 端口**。
   - 仅使用系统自带的客户端命令 `/usr/bin/mosquitto_sub` 和 `/usr/bin/mosquitto_pub` 向外部路由器（`192.168.1.1:1883`）发起常规客户端长连接，网络与进程空间完全隔离，对官方功能零侵入。
2. **免写 YAML 自动发现（HA MQTT Discovery）**：
   - 音箱启动联网后，自动向 Home Assistant 发送 MQTT Discovery 注册报文。
   - Home Assistant 自动生成交互实体，无需用户在 `configuration.yaml` 手写复杂配置。
3. **零额外环境依赖**：
   - 音箱端不安装 Python、Node 或重型运行时，纯利用系统内置的轻量 C 语言工具（`mosquitto_sub` / `mosquitto_pub`），内存占用仅 ~1MB。
4. **断电持久化**：
   - 所有配置与脚本存放在 `/data`（UBI 持久化可读写闪存分区），通过 `/data/init.sh` 保证音箱断电重启后自动恢复。

---

## 3. MQTT 通信协议与主题 (Topics) 规范

### 3.1 主题规划

| 主题 (Topic) | 传输方向 | QoS | Retain | 报文说明 |
|:---|:---:|:---:|:---:|:---|
| `homeassistant/select/xiaomi_sound_led/config` | 音箱 $\to$ Broker $\to$ HA | 1 | **true** | HA 自动发现注册报文（JSON 格式） |
| `xiaomi_sound/led/set` | HA $\to$ Broker $\to$ 音箱 | 1 | false | 模式切换控制指令 |
| `xiaomi_sound/led/state` | 音箱 $\to$ Broker $\to$ HA | 1 | **true** | 当前运行模式状态反馈 |
| `xiaomi_sound/led/availability` | 音箱 $\to$ Broker $\to$ HA | 1 | **true** | 音箱在线/离线健康状态（含遗嘱消息 LWT） |

---

### 3.2 控制指令与状态映射表

| 模式代码 | HA 下发 Payload (`xiaomi_sound/led/set`) | HA 显示名称 (Options) | 音箱执行逻辑 |
|:---:|:---|:---|:---|
| **`auto`** | `auto` | `自动轮换 (Auto Cycle)` | 启动律动，每 60 秒平滑轮换 1 $\to$ 2 $\to$ 3 $\to$ 4 |
| **`1`** | `1` | `模式 1: 双翼声学均衡器` | 锁定为模式 1（8 频段双翼 FFT 调音台） |
| **`2`** | `2` | `模式 2: 重低音大动态立体声` | 锁定为模式 2（低音双向爆破 + 峰值悬停） |
| **`3`** | `3` | `模式 3: 彩虹熔岩流动` | 锁定为模式 3（HSV 色相行波液态流动） |
| **`4`** | `4` | `模式 4: 全频律动` | 锁定为模式 4（18 频段连续色谱 + 90° 正面对齐） |
| **`off`** | `off` | `关闭律动 (恢复官方)` | 杀死 `led_music`，启动官方 `ledserver`，完全恢复小爱声光交互 |

---

### 3.3 Home Assistant 自动发现 JSON 报文格式

音箱启动或网络连通时，主动向 `homeassistant/select/xiaomi_sound_led/config` 发布如下 Retained 报文：

```json
{
  "name": "小米音箱声光律动",
  "unique_id": "xiaomi_sound_l06a_led_visualizer",
  "command_topic": "xiaomi_sound/led/set",
  "state_topic": "xiaomi_sound/led/state",
  "availability_topic": "xiaomi_sound/led/availability",
  "payload_available": "online",
  "payload_not_available": "offline",
  "icon": "mdi:music-note-outline",
  "options": [
    "auto",
    "1",
    "2",
    "3",
    "4",
    "off"
  ],
  "device": {
    "identifiers": ["xiaomi_sound_l06a"],
    "name": "Xiaomi Sound (L06A)",
    "model": "L06A",
    "manufacturer": "Xiaomi",
    "sw_version": "v1.0-beta2"
  }
}
```

> **提示**：为提升 HA UI 用户体验，可配置选项映射字典，或同时注册一个状态辅助实体，直接在卡片上呈现友好的中文模式名称。

---

## 4. 音箱侧守护进程实现机制

### 4.1 配置文件规划 (`/data/mqtt.conf`)

用于持久化保存外部 MQTT Broker 连接信息，默认免密开箱即用：

```ini
# /data/mqtt.conf
MQTT_HOST="192.168.1.1"
MQTT_PORT="1883"
MQTT_USER=""
MQTT_PASS=""
MQTT_CLIENT_ID="xiaomi_sound_l06a"
```

### 4.2 守护服务工作流程 (`/data/led_guard.sh` 增强)

```
        ┌────────────────────────────────────────────────────────┐
        │                 音箱开机 /data/init.sh                 │
        └───────────────────────────┬────────────────────────────┘
                                    │
                                    ▼
        ┌────────────────────────────────────────────────────────┐
        │            启动守护服务 /data/led_guard.sh             │
        └───────────────────────────┬────────────────────────────┘
                                    │
                     ┌──────────────┴──────────────┐
                     ▼                             ▼
        ┌─────────────────────────┐   ┌─────────────────────────┐
        │  线程 1: ubus 音频守护  │   │   线程 2: MQTT 通信守护 │
        │  • 3秒轮询放歌状态      │   │  • 发送 LWT 遗嘱 (offline)│
        │  • 放歌自动切入律动     │   │  • 发送 HA Discovery 包 │
        │  • 停歌恢复官方 led     │   │  • 发送 online 状态     │
        └─────────────────────────┘   └────────────┬────────────┘
                                                   │
                                                   ▼
                                      ┌─────────────────────────┐
                                      │ mosquitto_sub 阻塞监听  │
                                      │ Topic: xiaomi_sound/led/│
                                      │        set              │
                                      └────────────┬────────────┘
                                                   │ 收到控制 Payload
                                                   ▼
                                      ┌─────────────────────────┐
                                      │ • 写入 /data/led_mode   │
                                      │ • 热切换 led_music 模式 │
                                      │ • 回传 xiaomi_sound/led/│
                                      │   state 确认状态        │
                                      └─────────────────────────┘
```

### 4.3 核心 Bash 实现核心片段

```bash
# 1. 注册上线与遗嘱
send_ha_discovery() {
    local disc_topic="homeassistant/select/xiaomi_sound_led/config"
    local payload='{"name":"小米音箱声光律动","unique_id":"xiaomi_sound_led","command_topic":"xiaomi_sound/led/set","state_topic":"xiaomi_sound/led/state","availability_topic":"xiaomi_sound/led/availability","options":["auto","1","2","3","4","off"],"device":{"identifiers":["xiaomi_sound_l06a"],"name":"Xiaomi Sound","model":"L06A"}}'
    mosquitto_pub -h "$MQTT_HOST" -p "$MQTT_PORT" -t "$disc_topic" -m "$payload" -r
    mosquitto_pub -h "$MQTT_HOST" -p "$MQTT_PORT" -t "xiaomi_sound/led/availability" -m "online" -r
}

# 2. 后台持续监听与自动重连循环
start_mqtt_listener() {
    send_ha_discovery
    while true; do
        mosquitto_sub -h "$MQTT_HOST" -p "$MQTT_PORT" \
                      --will-topic "xiaomi_sound/led/availability" \
                      --will-payload "offline" --will-retain \
                      -t "xiaomi_sound/led/set" | while read -r cmd; do
            case "$cmd" in
                "1"|"2"|"3"|"4")
                    echo "$cmd" > /data/led_mode
                    killall -9 led_music 2>/dev/null
                    start_music_visualizer
                    mosquitto_pub -h "$MQTT_HOST" -p "$MQTT_PORT" -t "xiaomi_sound/led/state" -m "$cmd" -r
                    ;;
                "auto")
                    rm -f /data/led_mode
                    killall -9 led_music 2>/dev/null
                    start_music_visualizer
                    mosquitto_pub -h "$MQTT_HOST" -p "$MQTT_PORT" -t "xiaomi_sound/led/state" -m "auto" -r
                    ;;
                "off")
                    echo "off" > /data/led_mode
                    killall -9 led_music 2>/dev/null
                    /etc/init.d/led start 2>/dev/null
                    mosquitto_pub -h "$MQTT_HOST" -p "$MQTT_PORT" -t "xiaomi_sound/led/state" -m "off" -r
                    ;;
            esac
        done
        sleep 5  # 断线保护后自动重连
    done
}
```

### 4.4 状态反向同步机制（自动轮换同步到 HA）

当音箱处于 `auto` 自动轮换模式时，每隔 60 秒系统会自动切换模式并更新 `/tmp/visualizer_mode`。
守护进程在每秒检测时，如果发现当前模式发生变化，便会执行：
```bash
mosquitto_pub -h "$MQTT_HOST" -p "$MQTT_PORT" -t "xiaomi_sound/led/state" -m "$current_mode" -r
```
**你在手机 Home Assistant App 上的选择器组件会立刻同步更新显示当前实际正在运行的模式**，实现真正意义上的状态双向闭环。

---

## 5. Home Assistant 自动化与联动应用示例

接入 Home Assistant 后，可轻松利用 HA 自动化引擎实现家庭智能声光联动：

### 场景 1：夜间就寝自动柔和（避免刺眼）
```yaml
alias: "夜间自动切换至彩虹熔岩模式"
trigger:
  - platform: time
    at: "22:30:00"
condition:
  - platform: state
    entity_id: select.xiaomi_sound_led
    state: ["1", "2", "4", "auto"]
action:
  - service: select.select_option
    target:
      entity_id: select.xiaomi_sound_led
    data:
      option: "3"  # 切换到柔和治愈的彩虹熔岩
```

### 场景 2：派对 / 观影联动（低音炮爆发）
```yaml
alias: "派对模式联动"
trigger:
  - platform: state
    entity_id: input_boolean.party_mode
    to: "on"
action:
  - service: select.select_option
    target:
      entity_id: select.xiaomi_sound_led
    data:
      option: "4"  # 切换至爆发力十足的全频律动
```

---

## 6. 异常与边界情况处理

1. **网络断开或 MQTT Broker 维护**：
   - `mosquitto_sub` 异常退出后，外层 `while true` 循环会在 5 秒后自动尝试重新握手，无需重启音箱；
   - 断网期间，本地声光律动与放歌检测**丝毫不受影响**，依然按照本地 `/data/led_mode` 正常律动。
2. **多客户端并发控制防抖**：
   - 指令写入 `/data/led_mode` 后毫秒级下发，避免短时间内连续快速切换导致进程频繁拉起。
3. **安全与权限保护**：
   - 客户端连接严格限制在只发布和订阅 `xiaomi_sound/` 与 `homeassistant/` 命名空间，不接收任意 shell 注入指令，只严格匹配白名单参数（`1`, `2`, `3`, `4`, `auto`, `off`）。
