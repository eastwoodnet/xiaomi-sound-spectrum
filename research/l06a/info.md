# Xiaomi Sound (L06A) Hardware & System Research Info

Tested with official release firmware **1.66.11** (ROM Build: 2020-12-11).

---

## 1. Hardware Overview

| Component | Specification | Details |
| :--- | :--- | :--- |
| **Model** | Xiaomi Sound (L06A) | `xiaomi.wifispeaker.l06a` |
| **SoC** | Amlogic Meson AXG (A113X / A113D) | Quad-core ARM Cortex-A53 @ 1.5 GHz |
| **CPU Architecture** | ARMv8 AArch64 | 64-bit Kernel (4.9.61), mixed 32/64-bit userspace |
| **RAM** | 256 MB DDR3 | ~241 MB usable (`MemTotal: 247588 kB`) |
| **Flash ROM** | 128 MB SPI NAND Flash | Driven by `aml_nand` |
| **Audio Amplifier** | TI TAS5805M | 45W Stereo Digital-Input Class-D Amp (I2C-2 @ `0x2c`) |
| **Microphone Array** | PDM Digital Mic Array | High-SNR voice pick-up (`hw:0,3`) |
| **Hardware Loopback** | TDM-A Audio Loopback | Internal audio stream monitor (`hw:0,0`) |
| **Ring LED Controller** | Awinic AW20054 | 54-Channel LED Matrix Driver (I2C-0 @ `0x3a`), 18 RGB LEDs |
| **Touch Panel** | ETEK / Titan Micro ET6037 | Capacitive touch keys (I2C-0 @ `0x23`, `0x25`, `0x27`) |
| **Input / Buttons** | ADC Keypad | Platform ADC driver (`/dev/input/event0`) |
| **AUX-In Port** | 3.5mm Stereo Jack | Hardware insertion detect via GPIO-61 (`/dev/input/event1`) |
| **Wi-Fi & Bluetooth** | Marvell / NXP SD8xxx | 802.11 a/b/g/n/ac 2.4G/5G + BT 5.0 (SDIO interface) |

---

## 2. Flash Images and Partitions

### Partition Table (`/proc/mtd`)

```
dev:    size   erasesize  name
mtd0: 00200000 00020000 "bootloader"
mtd1: 00800000 00020000 "tpl"
mtd2: 00600000 00020000 "boot0"
mtd3: 00600000 00020000 "boot1"
mtd4: 02800000 00020000 "system0"
mtd5: 02800000 00020000 "system1"
mtd6: 01400000 00020000 "data"
```

### Partition Details

| Block Device | Size (Hex) | Size (Bytes) | Partition Name | Filesystem / Format | Description & Role |
| :--- | :--- | :--- | :--- | :--- | :--- |
| `/dev/mtdblock0` | `0x00200000` | 2,097,152 (2 MB) | `bootloader` | Raw Binary | Amlogic U-Boot bootloader (U-Boot 2015.01) |
| `/dev/mtdblock1` | `0x00800000` | 8,388,608 (8 MB) | `tpl` | FIP / TrustZone | Trusted Platform Loader (ATF / BL2 / BL31 / BL32 / OP-TEE) |
| `/dev/mtdblock2` | `0x00600000` | 6,291,456 (6 MB) | `boot0` | Android BootImg | Kernel Boot Partition A (AArch64 `Image.gz` + Device Tree `dtb`) |
| `/dev/mtdblock3` | `0x00600000` | 6,291,456 (6 MB) | `boot1` | Android BootImg | Kernel Boot Partition B (Dual-boot / OTA backup) |
| `/dev/mtdblock4` | `0x02800000` | 41,943,040 (40 MB) | `system0` | SquashFS 4.0 (xz) | Rootfs Partition A |
| `/dev/mtdblock5` | `0x02800000` | 41,943,040 (40 MB) | `system1` | SquashFS 4.0 (xz) | Rootfs Partition B (Currently active mounted at `/`) |
| `/dev/mtdblock6` | `0x01400000` | 20,971,520 (20 MB) | `data` | UBI / UBIFS | Writable user data partition (`/dev/ubi0_0` mounted at `/data`) |

### Mount Table (`df -h`)

```
Filesystem                Size      Used Available Use% Mounted on
/dev/mtdblock5           34.5M     34.5M         0 100% /
tmpfs                   120.9M      7.7M    113.2M   6% /tmp
tmpfs                   512.0K         0    512.0K   0% /dev
/dev/ubi0_0              13.4M    948.0K     11.8M   7% /data
```

> **Note on Persistency**:
> The root filesystem `/` is read-only SquashFS.
> The only writable persistent storage across reboots is `/data` (`/dev/ubi0_0`, ~13.4 MB available).
> Essential configurations under `/usr/share/mico/*.cfg` and `/etc/shadow` are bind-mounted directly from files stored in `/data`.

---

## 3. System & Operating Environment

### Kernel & OS Release

```
Linux L06A 4.9.61 #1 SMP PREEMPT Fri Dec 11 11:05:21 2020 aarch64 GNU/Linux

DISTRIB_ID='LEDE'
DISTRIB_RELEASE='SNAPSHOT'
DISTRIB_REVISION='70-1-1'
DISTRIB_TARGET='meson/axg_32'
DISTRIB_ARCH='arm_cortex-a9'
DISTRIB_DESCRIPTION='LEDE Reboot SNAPSHOT 70-1-1'
DISTRIB_TAINTS='no-all glibc busybox override'
```

- **Kernel**: Linux 4.9.61 (AArch64 64-bit SMP Preempt).
- **Toolchain**: Linaro GCC 6.3.1 (glibc based).
- **Userspace**: 32-bit `armhf` userland with 64-bit kernel execution capability.
- **Bootloader Arguments** (`/proc/cmdline`):
  ```
  rootfstype=ramfs init=/init console=ttyS0,115200 no_console_suspend quiet earlycon=aml_uart,0xff803000 jtag=apao reboot_mode=watchdog_reboot uboot=U-Boot 2015.01 (Dec 11 2020 - 11:36:42)
  ```

### Firmware Version Info (`/usr/share/mico/version`)

```
config core 'version'
	option ROM '1.66.11'
	option CHANNEL 'release'
	option UBOOT '0.0.1'
	option LINUX '0.0.1'
	option RAMFS '0.0.1'
	option SQAFS '0.0.1'
	option ROOTFS '0.0.1'
	option BUILDTIME 'Fri, 11 Dec 2020 19:16:48 +0800'
	option BUILDTS '1607685408'
	option GTAG 'commit b9e9b6640c2491c7a77a22612e47790e6c8c0356'
	option HARDWARE 'L06A'

config miio 'miio'
	option product_id '0000'
	option module 'xiaomi.wifispeaker.l06a'
	option ssid_prefix 'xiaomi-wifispeaker-l06a_miap'
```

---

## 4. Audio Subsystem (ALSA & Hardware)

### ALSA Sound Card (`/proc/asound/cards`)

```
 0 [AMLAXGSOUND    ]: AML-AXGSOUND - AML-AXGSOUND
                      AML-AXGSOUND
```

### Audio PCM Stream Nodes (`/proc/asound/pcm`)

```
00-00: TDM-A-dummy dummy-0 :  : playback 1 : capture 1
00-01: TDM-B-dummy dummy-1 :  : playback 1 : capture 1
00-02: TDM-C-tas5805 multicodec-2 :  : playback 1 : capture 1
00-03: PDM-dummy dummy-3 :  : capture 1
```

- **`hw:0,0` (TDM-A)**: **Hardware Audio Loopback Capture**.
  - Mirrors audio currently being played through the DSP without needing microphone input.
  - Used by visualizers (`led_music`) to capture real-time audio FFT:
    ```bash
    arecord -D hw:0,0 -c 2 -r 48000 -f S16_LE -t raw
    ```
- **`hw:0,2` (TDM-C)**: **Audio DAC / Amp Output (`tas5805`)**.
  - Connects to the Texas Instruments TAS5805M Class-D digital amplifier over I2S/TDM.
- **`hw:0,3` (PDM)**: **Microphone Array Audio Capture**.
  - PDM microphone bus for voice recognition and Wake-Word Engine (KWS).

### Sound Server & DSP Routing

- ALSA default plugin (`/etc/asound.conf`) routes audio through softvol and DTS Audio DSP processing (`slave.pcm dtsaudio`).
- Playback subsystems:
  - System media: `mediaplayer` / `mdplay`
  - Voice TTS: `qplayer` / `pnshelper`
  - Bluetooth audio: `mibluealsa` (A2DP sink)
  - DLNA / AirPlay: `dlna` / `linein`

---

## 5. Ring LED Lighting Subsystem (AW20054)

### Hardware Architecture

- **Driver IC**: Awinic AW20054 (54-channel constant current LED matrix controller).
- **Physical Layout**: 18 RGB LEDs in a circular ring under the top frosted light diffuser.
- **Bus**: I2C Bus 0 at slave address `0x3a` (`/sys/bus/i2c/devices/0-003a`).
- **Hardware Enable (HWEN)**: `gpio-18` (`aw20054-hwen-pin`).
- **Driver Module**: `aw20054.ko`.

### Sysfs Direct Control Interface

The driver exposes a direct kernel control node:
```
/sys/devices/i2c-0/0-003a/led_rgb
```

#### Writing to LEDs:

Format:
```bash
echo "<LED_INDEX> <HEX_RGB>" > /sys/devices/i2c-0/0-003a/led_rgb
```
- `<LED_INDEX>`: `0` to `17` (18 LEDs in ring).
- `<HEX_RGB>`: `0xRRGGBB` or decimal value (e.g. `0xFF0000` for Red, `0` for OFF).

#### Example Commands:
```bash
# Light up LED 0 in pure red
echo "0 0x0000FF" > /sys/devices/i2c-0/0-003a/led_rgb

# Turn off all 18 LEDs
for i in $(seq 0 17); do
    echo "$i 0" > /sys/devices/i2c-0/0-003a/led_rgb
done
```

### Official Userspace Daemon (`ledserver`)

- Official Xiaomi lighting animations are driven by `/bin/ledserver`.
- Exposes ubus API namespace: `ubus call led <method>`.
- Control commands:
  ```bash
  # Shutdown ring LED light (0 power consumption)
  ubus call led shut
  /bin/show_led c

  # Display red microphone mute indicator
  /bin/show_led 7
  ```
- **Conflict Note**: To run custom music visualizers (such as `led_music`), `ledserver` must be stopped (`killall -9 ledserver`), or I2C bus arbitration conflicts will occur.

---

## 6. GPIO Assignments (`/sys/kernel/debug/gpio`)

### `gpiochip0` (AOBUS Banks, Pin 0-14, Base: `0xff800014`)

| Pin | Label | Direction | State | Function |
| :--- | :--- | :--- | :--- | :--- |
| `gpio-5` | `?` | `out` | `lo` | Reserved / Power control |

### `gpiochip1` (Periphs Banks, Pin 15-100, Base: `0xff634480`)

| Pin | Label | Direction | State | Function |
| :--- | :--- | :--- | :--- | :--- |
| `gpio-18` | `aw20054-hwen-pin` | `out` | `hi` | AW20054 Ring LED Driver Hardware Enable |
| `gpio-21` | `codec_pdn` | `out` | `hi` | TAS5805M Audio Amplifier Power Down / Mute (Active Low) |
| `gpio-61` | `auxin_det` | `in` | `lo` (IRQ) | 3.5mm AUX Line-In Jack Insertion Detect |
| `gpio-69` | `sdio_wifi` | `out` | `hi` | Marvell Wi-Fi SDIO Power Enable |
| `gpio-78` | `sysfs` | `out` | `hi` | General system GPIO |
| `gpio-79` | `sdio_wifi` | `in` | `hi` | Marvell Wi-Fi SDIO Interrupt / Status |
| `gpio-83` | `bt_rfkill` | `in` | `hi` | Bluetooth RFKill Radio Control |

---

## 7. I2C Bus Layout

### I2C Adapter 0 (`/dev/i2c-0`)

| I2C Address | Kernel Driver | Device Description |
| :--- | :--- | :--- |
| `0x23` | `leds-et6037` | ET6037 Capacitive Touch Key Controller 1 |
| `0x25` | `leds-et6037` | ET6037 Capacitive Touch Key Controller 2 |
| `0x27` | `leds-et6037` | ET6037 Capacitive Touch Key Controller 3 |
| `0x3a` | `leds-aw20054` | Awinic AW20054 54-Channel RGB LED Ring Matrix |

### I2C Adapter 2 (`/dev/i2c-2`)

| I2C Address | Kernel Driver | Device Description |
| :--- | :--- | :--- |
| `0x2c` | `tas5805` | Texas Instruments TAS5805M Class-D Audio Amplifier |

---

## 8. Network & Wireless Interfaces

- **`wlan0`**: Main Wi-Fi station interface (connected to local router, e.g. `192.168.1.6`).
- **`uap0`**: Wi-Fi Access Point interface (used during initial Mi Home BLE/Wi-Fi provisioning).
- **`lo`**: Local loopback interface (`127.0.0.1`).
- Driver: `sd8xxx.ko` / `mlan.ko` (Marvell SDIO 802.11ac dual-band 2.4G/5G).

---

## 9. Key Ubus Services & Endpoints

| Ubus Namespace | Key Methods / Events | Description |
| :--- | :--- | :--- |
| `mediaplayer` | `player_get_play_status`, `player_get_volume`, `player_play_operation` | Audio playback status & transport control |
| `led` | `shut`, `set`, `get_status` | Official ring LED animation dispatch |
| `mibrain` | `asr_result`, `nlp_result`, `wake_up` | XiaoAI cloud AI brain & voice parsing |
| `miio` | `ota`, `get_prop`, `set_prop` | Xiaomi Smart Home Mi Home (MIIO) protocol handler |
| `mibt` / `mible` | `scan`, `connect`, `pair`, `status` | Bluetooth Classic (A2DP) and BLE Mesh gateway |
| `sound_effect` | `get_eq`, `set_eq`, `dts_mode` | Hardware DTS sound effects & EQ profiles |
| `system` | `reboot`, `info`, `upgrade` | Base system health & OTA controls |
| `linein` | `get_status`, `switch` | AUX 3.5mm line-in automatic switching |

---

## 10. Developer Tips & Gotchas

1. **Auto-Start on Boot**:
   To make custom scripts persist across reboots:
   Create `/data/init.sh` and ensure it is executed in background:
   ```bash
   chmod +x /data/init.sh
   ```
2. **Architecture Compilation**:
   - The CPU kernel is 64-bit (`aarch64`).
   - The pre-installed default dynamic libraries under `/lib` and `/usr/lib` are 32-bit (`armhf`).
   - **Best Practice for Custom Binaries**: Statically compile for `aarch64` (`clang -target aarch64-linux-gnu -static -nostdlib` or `rust target aarch64-unknown-linux-musl`). This avoids any 32-bit library dependencies or interpreter mismatch.
3. **Audio Capture without Microphone Interference**:
   - Never record from `hw:0,3` for visualizers (that is the mic array and will capture room ambient noise and conversations).
   - Always record from `hw:0,0` (TDM-A hardware loopback), which only streams audio data generated by music playback.
