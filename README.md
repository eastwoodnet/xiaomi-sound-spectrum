# 小米 Sound (L06A) 音乐声光律动系统 (v1.0-beta2)

[English](README_EN.md) | 简体中文

> **OH2P 1.62.2**：使用 `python3 deploy-oh2p.py <音箱IP>`（省略 IP 可交互输入）安装、后台启动并设置开机自启，原厂对话结束后自动恢复频谱。见 [OH2P 部署与运行说明](docs/oh2p.md)。
> 下文的部署、守护进程及 v1.0-beta2 退场动画说明对应 L06A；OH2P 使用独立启动器与 12 灯前排映射。

本项目专为**小米小爱音箱（型号：L06A / Xiaomi Sound）**设计，利用音箱顶部的 18 颗环形全彩 RGB LED，结合原生 aarch64 C 语言实现的 **1024 点定点 (Q14) Radix-2 FFT 频谱分析引擎**与智能守护进程，实现高质量的音频声光律动效果。

---

> 🎬 **实机实测律动效果演示视频**：[点击前往 YouTube 查看实机演示](https://www.youtube.com/shorts/diHwyLa1s8Q)

> ⚠️ **设备兼容性说明**：  
> 默认构建、`deploy.py` 与 `led_guard.sh` 对应 **小米 Sound（L06A，AW20054）**。另提供 **OH2P 1.62.2（AW21036）实验适配**，请使用其专用部署入口，验证范围见 [OH2P 文档](docs/oh2p.md#原厂交互与验证范围)。其他型号与固件因声卡路由及硬件布局不同，不保证可直接运行。

> 🔑 **前置条件**：  
> 音箱需开启 SSH root 权限。解锁方式可参考社区教程：[duhow/xiaoai-patch](https://github.com/duhow/xiaoai-patch/blob/master/research/lx06/install.md)。

---

## 📝 v1.0-beta2 更新说明 (Changelog)

- **修复环境杂音误触发**：闲置状态下完全关闭采集与律动进程，解决周围说话、走动导致灯光误跳的问题；
- **修复状态指示与白光残留**：修正退出流程与关灯指令，消除官方启动钩子导致的异常白色光环残留；
- **恢复官方原厂灯效**：闲置时交还控制权，唤醒小爱蓝色光环、滑动调节音量指示等官方交互 100% 正常；
- **新增双态退场仪式动画**：音乐停止后，根据麦克风静音状态自动执行对应动画（静音时红光双向渐亮满环常亮；未静音时柔和白光双向收拢熄灭）；
- **新增夜间时段自动调光**：识别 22:00~06:00 夜间时段，退场动画自动切换为微暗微光，避免夜间刺眼。

---

## 🌟 程序核心特点

### 1. 原生底层架构与零运行时依赖
- 核心律动引擎（`led_music.c`）采用纯 Linux 系统调用（`sys_clone`、`sys_pipe2`、`sys_read`、`sys_write` 等）直接驱动硬件，编译参数使用 `-nostdlib -static`。
- 无需 target sysroot，无 libc 运行时依赖，二进制体积仅约 **20KB**，常驻内存仅约 **200KB**。

### 2. 1024 点高精度定点 (Q14) Radix-2 FFT 频谱引擎
- 内置 512 点 $\sin / \cos$ 旋转因子查表与 1024 点汉宁窗（Hanning Window）查表，全程无浮点运算。
- 48kHz 采样率下频率分辨率达 **46.88 Hz/bin**，精准切分为 8 个核心声学频段：
  - **Band 0 (47~94 Hz)**: 超低音 (Sub-Bass)
  - **Band 1 (141~188 Hz)**: 低音瞬态 (Bass Punch)
  - **Band 2 (234~422 Hz)**: 中低频 (Low Mids)
  - **Band 3 (469~938 Hz)**: 核心中频人声 (Midrange)
  - **Band 4 (984~2156 Hz)**: 中高频泛音 (High Mids)
  - **Band 5 (2.2k~4.5 kHz)**: 存在感与打击感 (Presence)
  - **Band 6 (4.5k~8.4 kHz)**: 明亮高频 (Treble)
  - **Band 7 (8.5k~15.9 kHz)**: 空气感 (Air)

### 3. I2C Deadband 死区滤波与独立 AGC
- 8 个频段分别具有独立的动态自适应增益控制（AGC），快速捕捉瞬态（Attack）并平滑衰减（Decay）。
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
| **模式 1** *(推荐)* | **双翼 8 频段立体声均衡器** | • **左翼 (LED 1~8)**: 映射左声道 8 大频段，呈现红 $\to$ 橙 $\to$ 黄 $\to$ 绿 $\to$ 青 $\to$ 蓝 $\to$ 紫光谱<br>• **右翼 (LED 17~10)**: 对称映射右声道 8 大频段<br>• **顶部 (LED 0)**: 超低频共振重击爆破<br>• **底部 (LED 9)**: 极高频瞬态碰撞闪烁 |
| **模式 2** | **重低音大动态立体声律动** | • **对称展开**: 低音驱动光柱由 0 号位向下延伸<br>• **频带质心动态色温**: 随音乐宏观能量重心变化（低音烈焰红、高音电光蓝、旋律翡翠青）<br>• **Peak-Hold**: 光柱顶端悬浮白色峰值浮点（停留 ~170ms 缓降） |

> **默认启动自动轮换模式（Auto Cycle）**：每隔 60 秒在两大模式之间自动切换。

---

## 📁 目录文件清单

```text
├── led_music.c         # 原生 aarch64 C 语言律动引擎 (ALSA 硬件流 + 1024点 FFT)
├── led_guard.sh        # 智能动态声光律动守护服务 (ubus 探测 + 官方/律动自适应切换)
├── build.sh            # 宿主机交叉编译脚本 (基于 Clang/LLD，纯静态编译)
├── deploy.py           # 宿主机一键部署脚本 (自动化 SSH 部署与开机自启配置)
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

1. **本地编译**：
   ```bash
   ./build.sh
   ```
2. **部署到音箱**：
   ```bash
   # 方式 1：直接传入 IP 与密码
   python3 deploy.py <SPEAKER_IP> <SSH_PASSWORD>

   # 方式 2：交互式输入
   python3 deploy.py
   ```

部署脚本会自动上传二进制与守护脚本，配置 `/data/init.sh` 并启动后台常驻守护。

---

### 方案 B：手动部署

1. 执行 `./build.sh` 编译生成 `led_music`；
2. 将 `led_music` 与 `led_guard.sh` 复制到音箱 `/data/` 目录并赋予可执行权限：
   ```bash
   chmod +x /data/led_music /data/led_guard.sh
   ```
3. 在 `/data/init.sh` 中配置开机启动：
   ```sh
   #!/bin/sh
   /etc/init.d/led start 2>/dev/null
   killall -9 led_guard.sh 2>/dev/null
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
| 查看守护与音频进程 | `ps \| grep -E 'led_guard\|ledserver\|led_music'` |
| 停止律动服务 | `killall -9 led_guard.sh led_music arecord 2>/dev/null` |
| 恢复官方出厂 LED 服务 | `/etc/init.d/led start` |
| 彻底关闭全部灯光 | `ubus call led shut` |

---

## 🛠️ 硬件技术备忘

1. **LED 颜色格式为 BGR**：sysfs 节点 `/sys/devices/i2c-0/0-003a/led_rgb` 格式为 `0xBBGGRR`（`0xFF0000` 为纯蓝，`0x0000FF` 为纯红，`0x00FF00` 为纯绿）。
2. **驱动使能与电流限制**：AW20054 的 `/sys/devices/i2c-0/0-003a/led_fade` 初始化时需写入 `r 0xff`、`g 0xff`、`b 0xff` 配置通道电流，否则输出电流为 0mA。
3. **开机持久化机制**：系统启动完成时会执行 `/etc/rc.local`，其中已配置调用 `/data/init.sh`，位于可读写的 UBIFS 分区，断电重启配置保持有效。

---

## 📄 License

MIT License
