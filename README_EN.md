# Xiaomi Sound (L06A) Music Visualizer & Light Show (v1.1-beta)

English | [简体中文](README.md)

An ultra-low-latency music visualizer designed specifically for the **Xiaomi Xiaoai Smart Speaker (Model: L06A / Xiaomi Sound)**. It utilizes the top 18-LED circular RGB ring, powered by a native aarch64 C engine featuring a **1024-point fixed-point (Q14) Radix-2 FFT spectral analysis pipeline**, paired with an intelligent system-level companion daemon and full **Home Assistant native MQTT Discovery & remote control integration**.

---

> 🎬 **Demo Video**: [Watch the live demonstration on YouTube](https://www.youtube.com/shorts/diHwyLa1s8Q)

> ⚠️ **Compatibility Notice**:  
> Tested exclusively on **Xiaomi Sound (L06A)**. Other models (e.g., Xiaomi Sound Pro, Xiaoai Pro, Redmi Touch Display) feature different audio routing and LED drivers; compatibility is not guaranteed.

> 🔑 **Prerequisite**:  
> Requires SSH root access on the speaker. Refer to the community unlock guide: [duhow/xiaoai-patch](https://github.com/duhow/xiaoai-patch/blob/master/research/lx06/install.md).

---

## 📝 v1.1-beta Major Milestone Changelog

- **🎨 Dynamic Palettes & Built-in Themes (`/data/palettes.conf`)**:
  - **Complete Decoupling of Algorithms and Colors**: Added standalone configuration file `/data/palettes.conf` utilizing standard human-readable `#RRGGBB` hex codes;
  - **5 Built-in Curated Themes**:
    - `rainbow`: 🌈 **Classic Full Spectrum** (Acoustic high-fidelity band mapping)
    - `cyberpunk`: 🌆 **Cyberpunk Neon** (Neon magenta + electric cyan + incandescent peaks)
    - `ocean`: 🌊 **Deep Ocean Blue** (Midnight navy -> azure blue -> emerald wave)
    - `fire`: 🔥 **Blazing Inferno** (Ruby red + pure scarlet + solar orange + golden sparks, 80%+ red dominance)
    - `aurora`: 🌌 **Aurora Borealis** (Fluorescent green -> deep turquoise -> purple aurora)
    - `custom`: 🎨 **Custom Mode** (Geek-grade tuning for every band, LED, and fluid hue range)
  - **Zero-Interruption Hot-Reloading**: Touch `/tmp/reload_palette` to seamlessly apply new colors within 800ms without stopping audio capture or playback.
- **🏠 Home Assistant Theme Selection**:
  - Added `select.xiaomi_sound_l06a_theme` (Lighting Theme Style) for instant one-touch theme switching via the Home Assistant web/app UI.
- **🎬 24 FPS Cinematic Refresh Rate & Asymmetric Temporal Damping**:
  - Frame rate restructured to ~23.44 FPS (aggregating peak transients over 2 capture cycles), substantially eliminating flicker and eye strain;
  - Introduced asymmetric slew-rate damping (instant attack, gentle ~88% exponential decay) for silky, mercury-like visual dynamics.

---

## 📝 v1.0 Release Major Milestone Changelog

- **4 Acoustic Light Show Modes Finalized**:
  - Mode 1: Stereo 8-Band Equalizer (hardware FFT-driven);
  - Mode 2: Full-Ring Bass Pulse (spectral centroid color shifts + peak hold);
  - Mode 3: Rainbow Lava Wave (HSV phase shift fluid aurora);
  - Mode 4: Full-Spectrum Dynamics (18-band chromatic spectrum, 90° CCW acoustic alignment);
  - Auto-cycle every 60 seconds with smooth transitions.
- **Home Assistant Native MQTT Discovery & Control Integration**:
  - Automatically sends MQTT Discovery payloads on boot, creating an interactive card in Home Assistant with **zero manual YAML configuration**;
  - **Mode Selector (`select.xiaomi_sound_l06a_led_mode`)**: Dropdown select between all 4 modes, auto-rotation, or turn off;
  - **Power Switch (`switch.xiaomi_sound_l06a_visualizer_switch`)**: Quick one-touch toggle for the visualizer;
  - **Real-Time Sensor (`sensor.xiaomi_sound_l06a_current_mode`)**: Reflects currently active visualizer sub-mode or standby state in real-time;
  - **Bidirectional State Sync**: When speaker auto-rotates modes, HA UI updates instantaneously;
  - **Last Will and Testament (LWT)**: HA entity marks as unavailable when the speaker powers down or disconnects.
- **Client-Only Architecture & Decoupled Configuration**:
  - Runs purely as a lightweight outbound MQTT Client, **100% isolating the internal factory Mosquitto broker** (ensuring Bluetooth Mesh remains unaffected);
  - Standalone `/data/mqtt.conf` configuration file: Broker IP, port, credentials can be freely edited without touching scripts or recompiling.
- **Microphone Protection & System UI Compatibility**:
  - Audio capture is completely halted during idle periods, avoiding ambient noise false triggers;
  - System UI animations (Xiaoai wake-up ring, volume adjustment) work 100% normally during idle periods;
  - Dual-state exit ceremonies and automated night dimming (22:00~06:00).

---

## 🌟 Core Features

### 1. Direct Linux Syscalls & Zero Runtime Overhead
- Core visualizer (`led_music.c`) interacts directly with the Linux kernel using pure syscalls (`sys_clone`, `sys_pipe2`, `sys_read`, `sys_write`, etc.).
- Compiled with `-nostdlib -static` with zero libc dependencies. Binary size is only **~20KB** with resident memory footprint of **~200KB**.

### 2. 1024-Point Fixed-Point (Q14) Radix-2 FFT Engine
- Inlined 512-point $\sin / \cos$ twiddle factor lookup table and 1024-point Hanning window table with zero floating-point operations.
- Yields a frequency resolution of **46.88 Hz/bin** at 48kHz, aggregated into 8 core acoustic bands and 18-band chromatic spectrum.

### 3. I2C Deadband Filtering & Independent AGC
- Independent dynamic Automatic Gain Control (AGC) per band with rapid Attack tracking and exponential Decay.
- Color deadband delta filtering (`color_diff >= 6`) eliminates imperceptible LED fluctuations, reducing I2C bus traffic by 80% and maintaining single-core CPU usage at ~19% (~4.7% total system load).

### 4. Smart Dual-Mode Companion Daemon
- Background daemon (`led_guard.sh`) queries playback status every 3 seconds via the native system `ubus` IPC. During `sleep`, CPU consumption is strictly 0.00%.
- **Automatic Activation**: Starts the visualizer when music playback (voice streaming, DLNA, Bluetooth) begins.
- **Graceful Exit Ceremony**: Plays a smooth transition animation when playback stops, then hands control back to the official `ledserver`.
- **Track-Switching Protection**: Rapidly aborts exit animations if a new song starts playing within the buffer window.

---

## 🎨 Visualization Modes

| Mode | Name | Description |
| :---: | :--- | :--- |
| **Mode 1** | **Stereo 8-Band Equalizer** | • **Left Wing (LED 1~8)**: Maps 8 acoustic bands on the left channel from sub-bass to air (Red $\to$ Orange $\to$ Yellow $\to$ Green $\to$ Cyan $\to$ Blue $\to$ Purple)<br>• **Right Wing (LED 17~10)**: Symmetrical right-channel frequency mapping<br>• **Top (LED 0)**: Sub-bass kick impact beat drop<br>• **Bottom (LED 9)**: High-frequency transient shimmer |
| **Mode 2** | **Full-Ring Bass Pulse** | • **Symmetrical Spread**: Bass pulse expands downward from LED 0<br>• **Dynamic Spectral Centroid**: Shifts color temperature based on audio energy (Fiery red for bass-heavy, electric blue for highs, emerald green for vocal melodies)<br>• **DAW Peak-Hold**: Top peak indicators hover for ~170ms before smooth decay |
| **Mode 3** | **Rainbow Lava Wave** | • **HSV Phase Shift Wave**: Partial differential phase-coupling propagation, liquid aurora-like flow<br>• **Multi-Band Modulation**: Bass drives continuous rotation, mid-frequencies modulate saturation, highs trigger subtle shimmer sparkles<br>• **Calm & Healing**: Perfect for acoustic, ambient, and jazz with zero eye strain |
| **Mode 4** *(Recommended)* | **Full-Spectrum Dynamics** | • **18-Band Chromatic Spectrum**: 1/3-octave log distribution (55Hz~20kHz) covering all 18 LEDs<br>• **Peak Nonlinear Dynamics**: Threshold noise gate + quadratic power expansion, smooth ambient baseline with explosive peak bloom<br>• **90° CCW Acoustic Alignment**: High-energy dynamic vocal range perfectly aligned with the front LEDs |

> **Auto Cycle**: Automatically cycles through all 4 modes every 60 seconds. You can lock into a specific mode via Home Assistant or CLI anytime.

---

## 🏠 Home Assistant MQTT Integration

Full support for Home Assistant **MQTT Discovery**, automatically discovering entities without YAML configuration.

### 1. Generated Home Assistant Entities

| Entity Type | Entity ID | Function |
| :--- | :--- | :--- |
| **Select** | `select.xiaomi_sound_l06a_led_mode` | Dropdown selector: `自动轮换` (Auto), `模式 1`, `模式 2`, `模式 3`, `模式 4`, `关闭律动 (恢复官方)` |
| **Select** | `select.xiaomi_sound_l06a_led_theme` | Dropdown theme selector: `🌈 经典彩虹`, `🌆 赛博朋克`, `🌊 深海冰蓝`, `🔥 炽热烈焰`, `🌌 极光秘境`, `🎨 自定义调色` |
| **Switch** | `switch.xiaomi_sound_l06a_visualizer_switch` | Master visualizer switch: `ON` / `OFF` |
| **Sensor** | `sensor.xiaomi_sound_l06a_current_mode` | Real-time active sub-mode (e.g., `模式 4: 全频律动`) or `待机 (官方交互)` |

### 2. Configuration (`/data/mqtt.conf`)

Located at `/data/mqtt.conf` on the speaker, persisted across reboots:

```sh
# ==============================================================================
# Xiaomi Sound (L06A) Home Assistant MQTT Remote Control Configuration
# ==============================================================================

# MQTT Broker IP (default is router IP, e.g., 192.168.1.1)
MQTT_HOST="192.168.1.1"

# MQTT Broker Port (default 1883)
MQTT_PORT="1883"

# Username (leave empty if no authentication)
MQTT_USER=""

# Password (leave empty if no authentication)
MQTT_PASS=""

# Enable MQTT Remote Control (1 = enabled, 0 = disabled)
MQTT_ENABLED="1"
```

### 3. MQTT Topics

| Topic | Direction | Description |
| :--- | :---: | :--- |
| `xiaomi_sound/led/set` | HA $\to$ Speaker | Set visualizer mode (`1`, `2`, `3`, `4`, `auto`, `off`) |
| `xiaomi_sound/led/state` | Speaker $\to$ HA | Active mode state (Retained) |
| `xiaomi_sound/led/power/set` | HA $\to$ Speaker | Power command (`ON` / `OFF`) |
| `xiaomi_sound/led/power/state` | Speaker $\to$ HA | Power state (`ON` / `OFF`, Retained) |
| `xiaomi_sound/led/theme/set` | HA $\to$ Speaker | Set theme (`cyberpunk`, `fire`, `aurora`, `rainbow`, `ocean`, `custom`) |
| `xiaomi_sound/led/theme/state` | Speaker $\to$ HA | Active theme style state (Retained) |
| `xiaomi_sound/led/current_mode` | Speaker $\to$ HA | Real-time active sub-mode (Retained) |
| `xiaomi_sound/led/availability` | Speaker $\to$ HA | LWT availability (`online` / `offline`, Retained) |

---

## 🎨 Palettes & Themes Configuration (`/data/palettes.conf`)

The system features a modular palette engine configured via **`/data/palettes.conf`** on the speaker, using human-standard **`#RRGGBB`** hex codes.

### 1. Global Themes (`THEME`)
Most users can simply pick a theme in Home Assistant or set `THEME`:

```ini
THEME = aurora   # Options: rainbow / cyberpunk / ocean / fire / aurora / custom
```

| Theme ID | Name | Style Characteristics | Mode 3 Fluid Atmosphere |
| :--- | :--- | :--- | :--- |
| **`rainbow`** | 🌈 Classic Rainbow | Default full spectrum, high-fidelity acoustic mapping | 0° ~ 360° full rainbow circulating wave |
| **`cyberpunk`** | 🌆 Cyberpunk | Neon magenta + phantom indigo + electric cyan | 180° ~ 320° cyan to magenta liquid wave |
| **`ocean`** | 🌊 Ocean Blue | Midnight navy $\to$ azure sky $\to$ emerald wave | 160° ~ 240° deep ocean glacial wave |
| **`fire`** | 🔥 Blazing Inferno | Blood crimson $\to$ pure scarlet $\to$ solar orange $\to$ golden spark | 0° ~ 28° bubbling deep volcanic magma |
| **`aurora`** | 🌌 Aurora Borealis | Fluorescent green $\to$ turquoise $\to$ mystic purple | 90° ~ 280° mystic emerald & purple aurora fluid |
| **`custom`** | 🎨 Custom | Geek tuning, loads custom mode parameters below | Governed by `MODE3_HUE_MIN` and `MAX` |

### 2. Advanced Custom Tuning (when `THEME = custom`)
To build your own palette, set `THEME = custom` in `/data/palettes.conf`:

```ini
# [Mode 1: Stereo 8-Band Equalizer] (symmetric left/right, bass to treble)
MODE1_BAND0 = #FF0020   # Band 0: Sub-Bass
MODE1_BAND1 = #FF4500   # Band 1: Bass Punch
MODE1_BAND2 = #FFB500   # Band 2: Low Mids
MODE1_BAND3 = #30FF00   # Band 3: Core Vocal Midrange
MODE1_BAND4 = #00FFFF   # Band 4: High Mids
MODE1_BAND5 = #0075FF   # Band 5: Presence
MODE1_BAND6 = #5000FF   # Band 6: Treble
MODE1_BAND7 = #FF40FF   # Band 7: Air
MODE1_TOP_BASS   = #FF0010   # LED 0 (back): Bass pulse color
MODE1_BOT_TREBLE = #FFFFFF   # LED 9 (front): Treble spark color

# [Mode 2: Dynamic Full-Ring Bass Pulse]
MODE2_BASS_COLOR   = #FF0010   # Bass burst dominant color
MODE2_MID_COLOR    = #FF2800   # Mid vocal chorus dominant color
MODE2_TREBLE_COLOR = #FF8000   # High treble impact color
MODE2_PEAK_COLOR   = #FFF8E0   # Floating peak color
MODE2_BG_COLOR     = #180002   # Ambient standby glow

# [Mode 3: Fluid Lava Flow] (HSV hue range: 0-3600, 3600 = 360°)
MODE3_HUE_MIN = 1600   # Starting hue (e.g., 1600 = 160° cyan)
MODE3_HUE_MAX = 2400   # Ending hue (e.g., 2400 = 240° ocean blue)

# [Mode 4: 18-Band Full Spectrum Chromatic Ring] (18 comma-separated #RRGGBB colors)
MODE4_COLORS = #FF0000, #FF3000, #FF6000, #FFA000, #FFD000, #A0FF00, #00FF30, #00FF90, #00FFE0, #00E0FF, #0090FF, #0040FF, #2000FF, #7000FF, #C000FF, #FF00E0, #FF0080, #FF0030
```

### 3. Millisecond Hot-Reloading
After editing `/data/palettes.conf`, execute in the speaker shell:
```sh
touch /tmp/reload_palette
```
The engine reloads the new palette within ~800ms seamlessly without stopping music playback.

---

## 📁 File Manifest

```text
├── led_music.c         # Native aarch64 C visualization engine (ALSA capture + 1024-pt FFT + dynamic palettes)
├── led_guard.sh        # Smart companion daemon (ubus status detection + HA MQTT client)
├── mqtt.conf           # Home Assistant MQTT configuration template
├── palettes.conf       # Dynamic palette and theme configuration template (#RRGGBB)
├── build.sh            # Cross-compilation script (Clang/LLD, static strip)
├── deploy.py           # Automated SSH deployment and autostart configuration script
├── EFFECTS.md          # Comprehensive acoustic visualizer algorithm design doc
├── README.md           # Chinese Documentation
└── README_EN.md        # English Documentation
```

---

## 🛠️ Toolchain & Requirements

Compiled using native **LLVM / Clang** and **LLD** targeting `aarch64-linux-gnu` without requiring target sysroots or GCC cross-compilers.

### Host Dependencies:
* **Ubuntu / Debian**: `sudo apt-get install -y clang lld llvm python3 python3-pip && pip3 install paramiko cryptography`
* **Arch Linux**: `sudo pacman -S clang lld llvm python-paramiko python-cryptography`
* **macOS**: `brew install llvm python3 && pip3 install paramiko cryptography`

---

## 🚀 Build & Deployment

### Option A: One-Click Deployment (Recommended)

1. **Configure MQTT (Optional)**:  
   Edit `mqtt.conf` to set `MQTT_HOST`, `MQTT_USER`, and `MQTT_PASS` if your broker requires authentication or is on a non-default host.
2. **Build locally**:
   ```bash
   ./build.sh
   ```
3. **Deploy to speaker**:
   ```bash
   # Method 1: Specify IP and password directly
   python3 deploy.py <SPEAKER_IP> <SSH_PASSWORD>

   # Method 2: Interactive prompt
   python3 deploy.py
   ```

The script automatically uploads the binary, config, and daemon, sets up persistent autostart in `/data/init.sh`, and initializes the service.

---

### Option B: Manual Deployment

1. Compile the binary on your host:
   ```bash
   ./build.sh
   ```
2. Copy `led_music`, `led_guard.sh`, and `mqtt.conf` to `/data/` on the speaker and grant permissions:
   ```bash
   chmod +x /data/led_music /data/led_guard.sh
   ```
3. Configure persistent autostart in `/data/init.sh`:
   ```sh
   #!/bin/sh
   /etc/init.d/led start 2>/dev/null
   killall -9 led_guard.sh mosquitto_sub 2>/dev/null
   if [ -f /data/led_guard.sh ]; then
       /data/led_guard.sh >/dev/null 2>&1 &
   fi
   ```
4. Start the companion daemon:
   ```bash
   /data/led_guard.sh >/dev/null 2>&1 &
   ```

---

## 🎮 Administration Commands

When logged into the speaker via SSH:

| Action | Command |
| :--- | :--- |
| View active daemon, MQTT, and audio processes | `ps \| grep -E 'led_guard\|mosquitto_sub\|ledserver\|led_music'` |
| Stop visualizer & MQTT service | `killall -9 led_guard.sh mosquitto_sub led_music arecord 2>/dev/null` |
| Restore official stock LED service | `/etc/init.d/led start` |
| Turn off all LEDs completely | `ubus call led shut` |
| Check currently saved visualizer mode | `cat /data/led_mode` |

---

## 🛠️ Hardware Technical Notes

1. **BGR Color Encoding**: The sysfs node `/sys/devices/i2c-0/0-003a/led_rgb` expects `0xBBGGRR` byte order (`0xFF0000` is pure Blue, `0x0000FF` is pure Red, and `0x00FF00` is pure Green).
2. **Driver Current Configuration**: Channel currents must be initialized via `/sys/devices/i2c-0/0-003a/led_fade` (`r 0xff`, `g 0xff`, `b 0xff`); otherwise current defaults to 0mA and LEDs will not illuminate.
3. **Persistent Autostart**: System boot executes `/etc/rc.local`, which invokes `/data/init.sh` located on the writable UBIFS partition, surviving power cycles and reboots.
4. **MQTT Client Isolation**: System uses `/usr/bin/mosquitto_sub` and `mosquitto_pub` purely as clients, never modifying or stopping the internal `/etc/init.d/mosquitto` broker, guaranteeing 100% stability for Bluetooth Mesh.

---

## 📄 License

MIT License
