# 小米 L06A 音乐律动 LED 效果算法设计 (v2.0)

## 1. 灯带物理布局

18 颗 RGB LED 环形排列于音箱顶部。

| 索引 | 位置 | 说明 |
|------|------|------|
| 9 | 最前面 | 正面 |
| 0 | 最后面 | 背面 |
| 1-8 | 后半圈 | 从后(0)经左侧到前(9) |
| 10-17 | 前半圈 | 从前(9)经右侧到后(0) |

环形拓扑: `... → 7 → 8 → 9 → 10 → 11 → ... → 17 → 0 → 1 → ... → 8 → 9 → ...`

```
         9 (正面)
     8 /           \ 10
   7  |             | 11
   6  |  (俯视图)   | 12
   5  |             | 13
   4  |             | 14
    3  \           / 15
      2 \         / 16
       1   17
         0 (背面)
```

### 颜色格式

sysfs `led_rgb` 接口实际为 **BGR** 顺序 (0xBBGGRR)，与文档描述相反。

| 写入值 | 实际显示 |
|--------|----------|
| `0x0000FF` | 红色 |
| `0x00FF00` | 绿色 |
| `0xFF0000` | 蓝色 |

### 亮度约束

- 最低 30（避免全灭闪烁）
- 最高 220（避免刺眼）
- 待机微光: 0x100002（极暗深紫）

---

## 2. 效果 1: 双翼频谱均衡器 (Spectrum Equalizer)

### 2.1 视觉目标

左右声道独立显示的经典频谱分析仪。左翼 (LED 1-8) 映射左声道 8 个频段，右翼 (LED 17-10) 映射右声道 8 个频段，声场分离清晰。

### 2.2 LED 映射

| LED | 映射 | 声道 | 说明 |
|-----|------|------|------|
| 0 | 低频鼓心跳 | 双声道 | Sub-Bass 能量脉冲 |
| 1 | Band 0 | 左声道 | Sub-Bass |
| 2 | Band 1 | 左声道 | Bass Punch |
| ... | ... | 左声道 | ... |
| 8 | Band 7 | 左声道 | Air |
| 9 | 高频闪烁 | 双声道 | Treble 瞬态碰撞 |
| 17 | Band 0 | 右声道 | Sub-Bass |
| 16 | Band 1 | 右声道 | Bass Punch |
| ... | ... | 右声道 | ... |
| 10 | Band 7 | 右声道 | Air |

### 2.3 亮度计算

```
对每频段 b, 声道 ch:
    lev = level_{ch}[b]             // AGC 归一化 0-100
    color = PAL_SPECTRUM[b]
    if lev > 85:
        color = blend(color, WHITE, (lev - 85) * 6)
    output = scale(color, lev)      // lev 直接作为 0-100 亮度
```

### 2.4 锚点 LED

- **LED 0 (背面)**: `bass_energy = (level_l[0] + level_r[0]) / 2`，>50 时向白混合
- **LED 9 (正面)**: `treble_energy = (level_l[6] + level_r[6] + level_l[7] + level_r[7]) / 4`，>60 时闪白

---

## 3. 效果 2: 重低音动态律动 (Bass Dynamic Pulse)

### 3.1 视觉目标

重低音驱动灯条从顶部 LED 0 向两侧展翼延伸，全频段质心自适应控制流光色温（火红 → 翠绿 → 赛博青蓝），带 DAW 风格 Peak-Hold 峰值悬停白光指示。

### 3.2 算法

```
bass_avg_{ch} = (level_{ch}[0] * 2 + level_{ch}[1]) / 3
target_{ch} = bass_avg_{ch} * 8 / 100              // 展翼长度 0-8
spread_{ch}: 瞬时攻击, 每帧-1 衰减
peak_{ch}: 记录最远展翼位置, 保持 10 帧后每帧-1

色温: 以 total_bass / total_mid / total_treble 比例动态计算
  - bass > 50% → 火红系
  - treble > 35% → 赛博蓝紫系
  - 否则 → 翠绿黄过渡
```

### 3.3 渲染规则

- `LED[step] = active_color` (step ≤ spread)
- `LED[peak] = WHITE` (peak > spread, 悬停中)
- 其余 = C_BASE (微光)

---

## 4. 效果 3: 彩虹熔岩流动 (Lava Flow)

### 4.1 视觉目标

18 颗 LED 全亮，颜色像熔岩一样缓慢流动、翻滚、融合。全 RGB 色域，低频驱动色彩旋转，高频触发亮度脉冲闪烁。

### 4.2 算法 (HSV 色相行波模型)

与废弃的 RGB 白噪声 + 拉普拉斯扩散方案不同，本实现采用 HSV 色彩空间的色相行波模型，从物理上保证色彩流动性和饱和度。

#### 状态

```
hue[18]: 每颗 LED 的色相角 (0-3599, 即 0.0°-359.9°, 精度 0.1°)
```

#### 每帧更新 (由 arecord 硬件时钟驱动, ~21.33ms)

**Step 1: 低频驱动旋转速度**

```
bass = (level_l[0]*2 + level_l[1] + level_r[0]*2 + level_r[1]) / 6
base_speed = 3 + bass / 8       // 3 ~ 15 (0.1°/帧)
                                 // 安静时 ~5°/s, 激烈时 ~50°/s
```

**Step 2: 中频影响饱和度**

```
mid_energy = avg(level[2..5])    // 左右声道平均
saturation = 180 + mid_energy * 75 / 100    // 180 ~ 255
```

**Step 3: 色相旋转 + 邻域相位耦合 (Jacobi 式)**

```
保存 old_hue[] = hue[]
对每颗 LED i (环形取模):
    hue[i] = old_hue[i] + base_speed + rand(±3)

    diff_left  = wrap(old_hue[left]  - old_hue[i])   // [-1800, 1800]
    diff_right = wrap(old_hue[right] - old_hue[i])
    coupling = (diff_left + diff_right) / 8
    hue[i] += coupling

    hue[i] = normalize(hue[i], 0, 3600)
```

效果: 色相波沿环形传播，邻居趋于相近色相形成"色块"，但不会热寂（有持续旋转注入）。

**Step 4: 亮度计算**

```
base_brightness = 100 + avg_level * 120 / 100    // 100 ~ 220
treble_boost = max(0, (treble - 60) * 3/2)       // 0 ~ 60, 高频闪烁

v = base_brightness + treble_boost + rand(±10)
v += stereo_boost                                // 左半环偏左声道, 右半环偏右声道
v = clamp(v, 30, 220)

output = hsv_to_bgr(hue[i] / 10, saturation, v)
```

### 4.3 与废弃方案的对比

| 维度 | 废弃方案 (RGB 扩散+白噪声) | 新方案 (HSV 色相行波) |
|------|--------------------------|---------------------|
| 色彩空间 | RGB 独立通道操作 | HSV 色相环连续旋转 |
| 空间特征 | 拉普拉斯扩散→热寂 | 行波传播→持续流动 |
| 噪声类型 | 椒盐噪点/雪花屏 | 色相微扰→自然色温变化 |
| 频段利用 | 仅低频→扰动幅度 | 低频→旋转, 中频→饱和度, 高频→闪烁 |
| 立体声 | 无 | 左右半环亮度差异化 |

### 4.4 参数汇总

| 参数 | 安静 | 激烈 | 说明 |
|------|------|------|------|
| base_speed | 3 (0.3°/帧) | 15 (1.5°/帧) | 色相旋转速度 |
| saturation | 180 | 255 | HSV 饱和度 |
| brightness | 100 | 220 | 基础亮度 |
| treble_boost | 0 | 60 | 高频闪烁叠加 |
| coupling | 1/8 | 1/8 | 邻域耦合强度 |

---

## 5. 效果 4: 环形立体声频谱 (Ring Spectrum)

### 5.1 视觉目标

18 颗 LED 以对称镜像方式展示 8 个频段能量，左翼使用左声道数据，右翼使用右声道数据，保留立体声空间感。正面锚点为低音鼓心跳脉冲。

### 5.2 LED 映射 (对称镜像 + 立体声分离)

```
         9 (正面, 低音鼓心跳)
     8(L:B0) /           \ 10(R:B0)
   7(L:B1) |               | 11(R:B1)
   6(L:B2) |   (俯视图)    | 12(R:B2)
   5(L:B3) |               | 13(R:B3)
   4(L:B4) |               | 14(R:B4)
   3(L:B5)  \             / 15(R:B5)
    2(L:B6)  \           / 16(R:B6)
     1(L:B7)    17(R:B7)
         0 (背面, 全频段动态混色)
```

| LED 索引 | 映射 | 声道 | 说明 |
|----------|------|------|------|
| 9 | 低音鼓心跳 | 双声道 | 正面锚点, Sub-Bass 脉冲 |
| 8 | Band 0 | 左声道 | Sub-Bass (低频, 靠近正面) |
| 7 | Band 1 | 左声道 | Bass Punch |
| 6 | Band 2 | 左声道 | Low Mids |
| 5 | Band 3 | 左声道 | Midrange |
| 4 | Band 4 | 左声道 | High Mids |
| 3 | Band 5 | 左声道 | Presence |
| 2 | Band 6 | 左声道 | Treble |
| 1 | Band 7 | 左声道 | Air (高频, 靠近背面) |
| 0 | 全频段混色 | 双声道 | 背面锚点, 动态频谱加权混色 |
| 10 | Band 0 | 右声道 | Sub-Bass (低频, 靠近正面) |
| 11 | Band 1 | 右声道 | Bass Punch |
| 12 | Band 2 | 右声道 | Low Mids |
| 13 | Band 3 | 右声道 | Midrange |
| 14 | Band 4 | 右声道 | High Mids |
| 15 | Band 5 | 右声道 | Presence |
| 16 | Band 6 | 右声道 | Treble |
| 17 | Band 7 | 右声道 | Air (高频, 靠近背面) |

对称关系: `LED[9-k]` (左翼/左声道) 与 `LED[9+k]` (右翼/右声道) 映射同一频段 (k=1..8)。

### 5.3 色相调色板 (BGR 格式, 复用 PAL_SPECTRUM)

| 频段 | 频率范围 | 色相 | BGR 值 | 视觉 |
|------|----------|------|--------|------|
| Band 0 | 47-94 Hz | 红 | `0x0000FF` | 烈焰深红 |
| Band 1 | 141-188 Hz | 橙 | `0x0045FF` | 烈阳火橙 |
| Band 2 | 234-422 Hz | 黄 | `0x00B5FF` | 琥珀金黄 |
| Band 3 | 469-938 Hz | 绿 | `0x00FF30` | 荧光青翠 |
| Band 4 | 984-2156 Hz | 青 | `0xFFFF00` | 赛博明青 |
| Band 5 | 2.2k-4.5k Hz | 蓝 | `0xFF7500` | 天空湛蓝 |
| Band 6 | 4.5k-8.4k Hz | 靛 | `0xFF0050` | 极电靛蓝 |
| Band 7 | 8.5k-15.9k Hz | 紫 | `0xFF40FF` | 极光粉紫 |

### 5.4 亮度计算

```
对每频段 b, 声道 ch (左翼=左声道, 右翼=右声道):
    lev = level_{ch}[b]                 // AGC 归一化 0-100
    brightness_pct = 30 + lev * 70 / 100    // 30 ~ 100

    color = PAL_SPECTRUM[b]
    if lev > 85:
        color = blend(color, WHITE, (lev - 85) * 6)
    output = scale(color, brightness_pct)
```

注意: `brightness_pct` 严格限制在 0-100 范围内，确保 `scale_color()` 全动态范围有效。

### 5.5 锚点 LED

- **LED 9 (正面, 低音鼓心跳)**:
  ```
  bass_energy = (level_l[0] + level_r[0]) / 2
  if bass > 50: blend(红, 白, (bass-50)*2), scale(, bass)
  else: scale(红, bass)
  ```
- **LED 0 (背面, 全频段动态混色)**:
  ```
  以 bass/treble 在全频段中的占比动态选择色温:
  - bass占比 > 40%: 红橙系
  - treble占比 > 30%: 蓝紫系
  - 其他: 绿黄过渡
  scale(color, avg_level)
  ```

---

## 6. 共同基础设施

### 6.1 音频源

```
arecord -q -D hw:0,2 -f S16_LE -r 48000 -c 2 -t raw --period-size=1024 --buffer-size=4096
```

- 48kHz, 16-bit, 双声道
- 1024 采样点/帧 = 21.33ms
- 左右声道独立 FFT
- **阻塞读取作为天然帧同步, 不额外 nanosleep**

### 6.2 FFT

- 1024 点 Radix-2 定点 (Q14)
- 汉宁窗
- 频率分辨率: 48000/1024 = 46.88 Hz/bin

### 6.3 频段划分

| Band | Bin 范围 | 频率范围 |
|------|----------|----------|
| 0 | 1-2 | 47-94 Hz |
| 1 | 3-4 | 141-188 Hz |
| 2 | 5-9 | 234-422 Hz |
| 3 | 10-20 | 469-938 Hz |
| 4 | 21-46 | 984-2156 Hz |
| 5 | 47-95 | 2.2k-4.5k Hz |
| 6 | 96-180 | 4.5k-8.4k Hz |
| 7 | 181-340 | 8.5k-15.9k Hz |

每频段取 bin 内最大幅值。

### 6.4 AGC (自适应增益控制)

每频段、每声道独立:

```
high[b]: 峰值追踪 (attack 即时, decay 0.5%/帧)
low[b]:  谷值追踪 (attack 即时, decay 0.5%/帧)
diff = max(high - low, 3000)
raw = (energy - low) * 100 / diff, clamp 0-100

level[b]: 包络 (attack 即时, decay 12%/帧)
```

### 6.5 LED 写入

- 差量提交: 颜色变化 < 6 不写
- 格式: `echo <idx> 0xBBGGRR > led_rgb`
- 18 颗逐颗写入

### 6.6 静音处理

```
RMS < 16 持续 ~2 秒 → 暂停状态 (微光待机 C_BASE)
RMS < 16 持续 ~30 秒 → 停止状态 (全灭)
RMS > 16 → 恢复播放状态
```

### 6.7 模式切换

命令行参数:
- `./led_music 1` → 双翼频谱均衡器
- `./led_music 2` → 重低音动态律动
- `./led_music 3` → 彩虹熔岩流动
- `./led_music 4` → 环形立体声频谱
- `./led_music a` → 自动轮换 (4 模式, 每模式 ~60 秒)
- 无参数 → 默认模式 1, 自动轮换

---

## 7. 文件结构

```
led_music.c    # 统一实现 (含模式 1-4)
led_music      # 编译后 aarch64 静态二进制 (~24KB)
EFFECTS.md     # 本文档
```

所有渲染逻辑统一在 `led_music.c` 中，不再有独立的 `led_effects.c`。
