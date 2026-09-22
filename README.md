# 小米 Sound (L06A) 音乐声光律动系统 (v1.0 Release)

[English](README_EN.md) | 简体中文

> **OH2P 1.62.2**：使用 `python3 deploy-oh2p.py <音箱IP>`（省略 IP 可交互输入）安装、后台启动并设置开机自启，原厂对话结束后自动恢复频谱。见 [OH2P 部署与运行说明](docs/oh2p.md)。
> 四种模式共用 18→12 像素映射。下文的部署、MQTT 控制及退场动画对应 L06A；OH2P 使用独立启动器。

本项目专为**小米小爱音箱（型号：L06A / Xiaomi Sound）**设计，利用音箱顶部的 18 颗环形全彩 RGB LED，结合原生 aarch64 C 语言实现的 **1024 点定点 (Q14) Radix-2 FFT 频谱分析引擎**与智能守护进程，实现专业级低延迟音频声光律动，并全面支持 **Home Assistant 原生 MQTT 自动发现与远端控制**。

---

> 🎬 **实机实测律动效果演示视频**：[点击前往 YouTube 查看实机演示](https://www.youtube.com/shorts/diHwyLa1s8Q)

> ⚠️ **设备兼容性说明**：  
> 默认构建、`deploy.py` 与 `led_guard.sh` 对应 **小米 Sound（L06A，AW20054）**。另提供 **OH2P 1.62.2（AW21036）实验适配**，请使用其专用部署入口，验证范围见 [OH2P 文档](docs/oh2p.md#原厂交互与验证范围)。其他型号与固件因声卡路由及硬件布局不同，不保证可直接运行。

> 🔑 **前置条件**：  
> 音箱需开启 SSH root 权限。解锁方式可参考社区教程：[duhow/xiaoai-patch](https://github.com/duhow/xiaoai-patch/blob/master/research/lx06/install.md)。

---

## 📝 v1.0 Release 重大里程碑更新说明 (Changelog)

- **四大专业级声光律动模式正式定型**：
  - 模式 1: 双翼 8 频段真·声学均衡器 (纯硬件 FFT 驱动)；
  - 模式 2: 重低音大动态立体声律动 (动态色温 + 峰值悬停)；
  - 模式 3: 彩虹熔岩流动 (HSV 色相行波)；
  - 模式 4: 全频律动 (18 频段连续色谱与峰值动力学，逆时针 90° 声学正面对齐)；
  - 支持 60 秒平滑自动循环轮换（Auto Cycle）。
- **全面集成 Home Assistant 原生 MQTT 自动发现 (MQTT Discovery)**：
  - 开机自动向 HA 发送注册报文，**零 YAML 配置**即可在 HA 设备列表中自动生成完整控制卡片；
  - **模式选择器 (`select.xiaomi_sound_l06a_led_mode`)**：支持在 HA 中下拉自由切换四大模式、自动轮换与关闭律动；
  - **律动总开关 (`switch.xiaomi_sound_l06a_visualizer_switch`)**：一键开启或关闭声光律动；
  - **实时状态传感器 (`sensor.xiaomi_sound_l06a_current_mode`)**：毫秒级实时回传当前物理运行的律动子模式或待机状态；
  - **双向双速状态同步**：音箱端自动轮换切变时实时同步回传，HA 界面状态实时更新；
  - **LWT 离线遗嘱**：音箱断电或离线时，HA 实体自动呈现不可用，避免误操作。
- **纯客户端隔离与解耦配置**：
  - 音箱作为轻量 MQTT Client 运行，**100% 隔离音箱内置原厂 MQTT Broker**，保证蓝牙 Mesh 核心通信不受任何干扰；
  - 独立配置文件 `/data/mqtt.conf`，Broker IP、端口、认证账号密码自由配置，更换局域网网络无需重编代码或改动脚本。
- **环境静音与系统交互 100% 兼容**：
  - 闲置状态下彻底关闭音频采集与硬件占用，麦克风保护防误跳；
  - 闲置时交还官方控制权，小爱唤醒光环与音量滑动调节 100% 正常；
  - 双态过渡退场仪式动画与夜间时段自动调光保护。

---

## 🌟 程序核心特点

### 1. 原生底层架构与零运行时依赖
- 核心律动引擎（`led_music.c`）采用纯 Linux 系统调用（`sys_clone`、`sys_pipe2`、`sys_read`、`sys_write` 等）直接驱动硬件，编译参数使用 `-nostdlib -static`。
- 无需 target sysroot，无 libc 运行时依赖，二进制体积仅约 **20KB**，常驻内存仅约 **200KB**。

### 2. 1024 点高精度定点 (Q14) Radix-2 FFT 频谱引擎
- 内置 512 点 $\sin / \cos$ 旋转因子查表与 1024 点汉宁窗（Hanning Window）查表，全程无浮点运算。
- 48kHz 采样率下频率分辨率达 **46.88 Hz/bin**，精准切分为 8 个核心声学频段与 18 个全频连续色谱。

### 3. I2C Deadband 死区滤波与独立 AGC
- 独立动态自适应增益控制（AGC），快速捕捉瞬态（Attack）并平滑衰减（Decay）。
- 针对 AW20054 I2C 总线引入色彩差异死区（`color_diff >= 6`）差量更新机制，过滤肉眼不可见的微小电平波动，减少 80% 的总线写入量，单核 CPU 占用率控制在 ~19%（全系统总体占用率约 4.7%）。

### 4. 智能双模生命周期守护
- 后台守护脚本（`led_guard.sh`）以 3 秒间隔通过系统 `ubus` 查询播放状态，在 `sleep` 挂起期间 CPU 占用为 0.00%。
- **放歌自动唤醒**：检测到音乐播放（语音点播 / DLNA / 蓝牙）时，自动暂停官方灯光服务并启动律动引擎。
- **停播优雅退场**：音乐停止后自动执行过渡退场动画并切回官方 `ledserver`，闲置时完全黑屏，小爱唤醒光环随时待命。
- **切歌防抖保护**：在退场动画执行期间切到下一首音乐时，毫秒级无缝打断退场并继续保持律动。

---

## 🎨 视觉律动模式

| 模式 | 名称 | 说明 |
| :---: | :--- | :--- |
| **模式 1** | **双翼 8 频段立体声均衡器** | • **左翼 (LED 1~8)**: 映射左声道 8 大频段，呈现红 $\to$ 橙 $\to$ 黄 $\to$ 绿 $\to$ 青 $\to$ 蓝 $\to$ 紫光谱<br>• **右翼 (LED 17~10)**: 对称映射右声道 8 大频段<br>• **顶部 (LED 0)**: 超低频共振重击爆破<br>• **底部 (LED 9)**: 极高频瞬态碰撞闪烁 |
| **模式 2** | **重低音大动态立体声律动** | • **对称展开**: 低音驱动光柱由 0 号位向下延伸<br>• **频带质心动态色温**: 随音乐宏观能量重心变化（低音烈焰红、高音电光蓝、旋律翡翠青）<br>• **Peak-Hold**: 光柱顶端悬浮白色峰值浮点（停留 ~170ms 缓降） |
| **模式 3** | **彩虹熔岩流动** | • **HSV 色相行波模型**: 偏微分相位耦合传播，液态色彩如极光顺滑流淌<br>• **多频联动**: 低频驱动旋转，中频影响饱和度，高频激发微弱光斑闪烁<br>• **柔和治愈**: 专为纯音乐、爵士、轻音乐设计，绝无频闪 |
| **模式 4** *(推荐)* | **全频律动** | • **18 频段等比连续色谱**: 专业 1/3 倍频程 55Hz~20kHz 闭环彩虹环<br>• **峰值非线性动力学**: 二次方非线性幂律放大与门限过滤，静如处子，动如脱兔<br>• **90° 逆时针声光对齐**: 听感最丰富的高动态活跃区完美正对音箱正前方爆发 |

> **默认启动自动轮换模式（Auto Cycle）**：每隔 60 秒在四大模式之间自动平滑切换。也可通过 Home Assistant 或命令行随时锁定特定模式。

---

## 🏠 Home Assistant MQTT 远端控制与自动发现

本项目全面支持 Home Assistant 的 **MQTT Discovery** 标准协议，无需繁琐的手写 YAML 配置。音箱启动后将自动注册为名为 **Xiaomi Sound** 的智能家居设备。

### 1. 自动生成的 Home Assistant 实体

| 实体类型 | 实体 ID | 功能与作用 |
| :--- | :--- | :--- |
| **Select (选择器)** | `select.xiaomi_sound_l06a_led_mode` | 下拉切换模式：`自动轮换`、`模式 1`、`模式 2`、`模式 3`、`模式 4`、`关闭律动 (恢复官方)` |
| **Switch (开关)** | `switch.xiaomi_sound_l06a_visualizer_switch` | 律动总开关：控制律动引擎开启（ON）或恢复官方状态（OFF） |
| **Sensor (传感器)** | `sensor.xiaomi_sound_l06a_current_mode` | 实时状态：动态显示当前音箱真实运行的子模式（如 `模式 4: 全频律动`）或 `待机 (官方交互)` |

### 2. MQTT 配置文件说明 (`/data/mqtt.conf`)

配置文件存放在音箱 `/data/mqtt.conf`，断电重启自动保持：

```sh
# ==============================================================================
# 小米 Sound (L06A) Home Assistant MQTT 远端控制配置文件
# ==============================================================================

# MQTT Broker 主机地址 (默认局域网主路由，例如 192.168.1.1)
MQTT_HOST="192.168.1.1"

# MQTT Broker 端口 (默认 1883)
MQTT_PORT="1883"

# 认证用户名 (无认证请留空)
MQTT_USER=""

# 认证密码 (无认证请留空)
MQTT_PASS=""

# 是否启用 MQTT 远端控制 (1 = 启用, 0 = 禁用)
MQTT_ENABLED="1"
```

> 💡 **配置修改方法**：  
> 若 MQTT Broker 地址变更或启用了账号密码，只需 SSH 登录音箱编辑 `/data/mqtt.conf`，随后执行 `killall -9 led_guard.sh` 即可自动重启加载新配置，无需重新编译或覆盖二进制。

### 3. MQTT 通信主题 (Topics)

| 主题 (Topic) | 方向 | 说明 |
| :--- | :---: | :--- |
| `xiaomi_sound/led/set` | HA $\to$ 音箱 | 下发模式设定（支持传入 `1`, `2`, `3`, `4`, `auto`, `off` 或对应中文名） |
| `xiaomi_sound/led/state` | 音箱 $\to$ HA | 回传当前设定模式（Retained 保持消息） |
| `xiaomi_sound/led/power/set` | HA $\to$ 音箱 | 下发开关指令（`ON` / `OFF`） |
| `xiaomi_sound/led/power/state` | 音箱 $\to$ HA | 回传总开关状态（`ON` / `OFF`，Retained） |
| `xiaomi_sound/led/current_mode` | 音箱 $\to$ HA | 实时物理运行子模式状态（Retained） |
| `xiaomi_sound/led/availability` | 音箱 $\to$ HA | 遗嘱与在线状态（`online` / `offline`，Retained） |

---

## 📁 目录文件清单

```text
├── led_music.c         # 原生 aarch64 C 语言律动引擎 (ALSA 硬件流 + 1024点 FFT)
├── led_guard.sh        # 智能动态声光律动守护服务 (ubus 探测 + HA MQTT 客户端)
├── mqtt.conf           # Home Assistant MQTT 远端控制配置文件模板
├── build.sh            # 宿主机交叉编译脚本 (基于 Clang/LLD，纯静态编译)
├── deploy.py           # 宿主机一键部署脚本 (自动化 SSH 部署、配置文件分发与开机自启)
├── EFFECTS.md          # 详细四大声光律动算法与声学设计文档
├── README.md           # 中文说明文档
└── README_EN.md        # English Documentation
```

---

## 🛠️ 编译与依赖要求

编译基于 **LLVM / Clang** 和 **LLD**，利用其原生多目标生成能力直接编译 ARM64 裸 ELF，无需 target sysroot 或 GNU cross 工具链。

### 依赖安装：
* **Ubuntu / Debian**: `sudo apt-get install -y clang lld llvm python3 python3-pip && pip3 install paramiko cryptography`
* **Arch Linux**: `sudo pacman -S clang lld llvm python-paramiko python-cryptography`
* **macOS**: `brew install llvm python3 && pip3 install paramiko cryptography`

---

## 🚀 编译与部署

### 方案 A：一键部署（推荐）

1. **配置 MQTT 参数（可选）**：  
   根据您的网络环境，编辑本地 `mqtt.conf` 中的 `MQTT_HOST`、`MQTT_USER` 与 `MQTT_PASS`（默认 Broker 为 `192.168.1.1:1883` 无密码）。
2. **本地编译**：
   ```bash
   ./build.sh
   ```
3. **部署到音箱**：
   ```bash
   # 方式 1：直接传入 IP 与密码
   python3 deploy.py <SPEAKER_IP> <SSH_PASSWORD>

   # 方式 2：交互式输入
   python3 deploy.py
   ```

部署脚本会自动上传二进制、配置文件与守护脚本，配置 `/data/init.sh` 并启动后台常驻守护。音箱端会自动向 MQTT Broker 注册并连通 Home Assistant。

---

### 方案 B：手动部署

1. 执行 `./build.sh` 编译生成 `led_music`；
2. 将 `led_music`、`led_guard.sh` 与 `mqtt.conf` 复制到音箱 `/data/` 目录并赋予权限：
   ```bash
   chmod +x /data/led_music /data/led_guard.sh
   ```
3. 在 `/data/init.sh` 中配置开机启动：
   ```sh
   #!/bin/sh
   /etc/init.d/led start 2>/dev/null
   killall -9 led_guard.sh mosquitto_sub 2>/dev/null
   if [ -f /data/led_guard.sh ]; then
       /data/led_guard.sh >/dev/null 2>&1 &
   fi
   ```
4. 执行 `/data/led_guard.sh >/dev/null 2>&1 &` 启动服务。

---

## 🎮 常用管理指令

登录音箱终端后：

| 操作 | 命令 |
| :--- | :--- |
| 查看守护、MQTT 及音频进程 | `ps \| grep -E 'led_guard\|mosquitto_sub\|ledserver\|led_music'` |
| 停止律动与 MQTT 服务 | `killall -9 led_guard.sh mosquitto_sub led_music arecord 2>/dev/null` |
| 恢复官方出厂 LED 服务 | `/etc/init.d/led start` |
| 彻底关闭全部灯光 | `ubus call led shut` |
| 查看当前保存的律动模式 | `cat /data/led_mode` |

---

## 🛠️ 硬件技术备忘

1. **LED 颜色格式为 BGR**：sysfs 节点 `/sys/devices/i2c-0/0-003a/led_rgb` 格式为 `0xBBGGRR`（`0xFF0000` 为纯蓝，`0x0000FF` 为纯红，`0x00FF00` 为纯绿）。
2. **驱动使能与电流限制**：AW20054 的 `/sys/devices/i2c-0/0-003a/led_fade` 初始化时需写入 `r 0xff`、`g 0xff`、`b 0xff` 配置通道电流，否则输出电流为 0mA。
3. **开机持久化机制**：系统启动完成时会执行 `/etc/rc.local`，其中已配置调用 `/data/init.sh`，位于可读写的 UBIFS 分区，断电重启配置保持有效。
4. **MQTT 客户端隔离保障**：系统调用标准 `/usr/bin/mosquitto_sub` 与 `mosquitto_pub` 外联控制，绝不重启或修改音箱原生 `/etc/init.d/mosquitto` 本地代理服务，确保米家 Mesh 网关稳定可靠。

---

## 📄 License

MIT License
