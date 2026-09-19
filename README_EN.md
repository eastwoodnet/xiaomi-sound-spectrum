# Xiaomi Sound (L06A) Music Visualizer & Light Show (v1.0-beta2)

English | [简体中文](README.md)

An ultra-low-latency music visualizer for the **Xiaomi Xiaoai Smart Speaker (Model: L06A / Xiaomi Sound)**. It utilizes the top 18-LED circular RGB ring, driven by a native aarch64 C engine featuring a **1024-point fixed-point (Q14) Radix-2 FFT spectral analysis pipeline** paired with an intelligent system-level companion daemon.

---

> 🎬 **Demo Video**: [Watch the live demonstration on X (Twitter)](https://x.com/eastwoodnet/status/2100857481794605311)

> ⚠️ **Compatibility Notice**:  
> Tested exclusively on **Xiaomi Sound (L06A)**. Other models (e.g., Xiaomi Sound Pro, Xiaoai Pro, Redmi Touch Display) feature different audio routing and LED drivers; compatibility is not guaranteed.

> 🔑 **Prerequisite**:  
> Requires SSH root access on the speaker. Refer to the community unlock guide: [duhow/xiaoai-patch](https://github.com/duhow/xiaoai-patch/blob/master/research/lx06/install.md).

---

## 📝 Changelog (v1.0-beta2)

- **Fixed ambient noise false triggers**: Audio capture process is now completely terminated during idle periods, preventing room chatter and footsteps from triggering LED animations.
- **Fixed indicator status & white ring residue**: Corrected shutdown sequencing to eliminate accidental white light ring caused by system startup hooks.
- **Restored official system UI lighting**: Official `ledserver` takes over during idle periods; voice assistant blue wake-up ring, volume adjustment arc, and system alerts work normally.
- **Added dual-state exit ceremonies**: When music stops, the system plays an exit animation depending on microphone status (fills to solid red when muted; expands and symmetrically collapses to dark when unmuted).
- **Added night-mode auto-dimming**: Automatically detects night hours (22:00–06:00) and dims exit animations to moonlight intensity to prevent glare in dark rooms.

---

## 🌟 Core Features

### 1. Direct Linux Syscalls & Zero Runtime Overhead
- Core visualizer (`led_music.c`) interacts directly with the Linux kernel using pure syscalls (`sys_clone`, `sys_pipe2`, `sys_read`, `sys_write`, etc.).
- Compiled with `-nostdlib -static` with zero libc dependencies. Binary size is only **~20KB** with resident memory footprint of **~200KB**.

### 2. 1024-Point Fixed-Point (Q14) Radix-2 FFT Engine
- Inlined 512-point $\sin / \cos$ twiddle factor lookup table and 1024-point Hanning window table with zero floating-point operations.
- Yields a frequency resolution of **46.88 Hz/bin** at 48kHz, aggregated into 8 acoustic bands:
  - **Band 0 (47~94 Hz)**: Sub-Bass
  - **Band 1 (141~188 Hz)**: Bass Punch
  - **Band 2 (234~422 Hz)**: Low Mids
  - **Band 3 (469~938 Hz)**: Midrange / Vocals
  - **Band 4 (984~2156 Hz)**: High Mids
  - **Band 5 (2.2k~4.5 kHz)**: Presence
  - **Band 6 (4.5k~8.4 kHz)**: Treble
  - **Band 7 (8.5k~15.9 kHz)**: Air

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
| **Mode 1** *(Default)* | **Stereo 8-Band Equalizer** | • **Left Wing (LED 1~8)**: Maps 8 acoustic bands on the left channel from sub-bass to air (Red $\to$ Orange $\to$ Yellow $\to$ Green $\to$ Cyan $\to$ Blue $\to$ Purple)<br>• **Right Wing (LED 17~10)**: Symmetrical right-channel frequency mapping<br>• **Top (LED 0)**: Sub-bass kick impact beat drop<br>• **Bottom (LED 9)**: High-frequency transient shimmer |
| **Mode 2** | **Full-Ring Bass Pulse** | • **Symmetrical Spread**: Bass pulse expands downward from LED 0<br>• **Dynamic Spectral Centroid**: Shifts color temperature based on audio energy (Fiery red for bass-heavy, electric blue for highs, emerald green for vocal melodies)<br>• **DAW Peak-Hold**: Top peak indicators hover for ~170ms before smooth decay |

> **Auto Cycle**: Automatically cycles between Mode 1 and Mode 2 every 60 seconds.

---

## 📁 File Manifest

```text
├── led_music.c         # Native aarch64 C visualization engine (ALSA capture + 1024-pt FFT)
├── led_guard.sh        # Smart companion daemon (ubus status detection + auto switching)
├── build.sh            # Cross-compilation script (Clang/LLD, static strip)
├── deploy.py           # Automated SSH deployment and autostart configuration script
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

1. **Build locally**:
   ```bash
   ./build.sh
   ```
2. **Deploy to speaker**:
   ```bash
   # Method 1: Specify IP and password directly
   python3 deploy.py <SPEAKER_IP> <SSH_PASSWORD>

   # Method 2: Interactive prompt
   python3 deploy.py
   ```

The script automatically uploads the binary and daemon, sets up persistent autostart in `/data/init.sh`, and initializes the service.

---

### Option B: Manual Deployment

1. Compile the binary on your host:
   ```bash
   ./build.sh
   ```
2. Copy `led_music` and `led_guard.sh` to `/data/` on the speaker and grant execution permissions:
   ```bash
   chmod +x /data/led_music /data/led_guard.sh
   ```
3. Configure persistent autostart in `/data/init.sh`:
   ```sh
   #!/bin/sh
   /etc/init.d/led start 2>/dev/null
   killall -9 led_guard.sh 2>/dev/null
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
| View active visualizer and audio processes | `ps \| grep -E 'led_guard\|ledserver\|led_music'` |
| Stop visualizer service | `killall -9 led_guard.sh led_music arecord 2>/dev/null` |
| Restore official stock LED service | `/etc/init.d/led start` |
| Turn off all LEDs completely | `ubus call led shut` |

---

## 🛠️ Hardware Technical Notes

1. **BGR Color Encoding**: The sysfs node `/sys/devices/i2c-0/0-003a/led_rgb` expects `0xBBGGRR` byte order (`0xFF0000` is pure Blue, `0x0000FF` is pure Red, and `0x00FF00` is pure Green).
2. **Driver Current Configuration**: Channel currents must be initialized via `/sys/devices/i2c-0/0-003a/led_fade` (`r 0xff`, `g 0xff`, `b 0xff`); otherwise current defaults to 0mA and LEDs will not illuminate.
3. **Persistent Autostart**: System boot executes `/etc/rc.local`, which invokes `/data/init.sh` located on the writable UBIFS partition, surviving power cycles and reboots.

---

## 📄 License

MIT License
