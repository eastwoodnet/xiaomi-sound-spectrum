/* ==============================================================================
 * 小米 Sound (L06A) ALSA Loopback + 1024 点定点 FFT 音乐律动守护进程 v4.1 (极速优化版)
 *
 * 核心架构与技术突破:
 * 1. 彻底废除 SAR ADC 模拟采样，全面接入 hw:0,2 (TDM-C) 48kHz/16-bit 硬件数字回环
 * 2. 直通 Linux 系统调用 (sys_pipe2 + sys_clone + sys_execve) 驱动 arecord
 *    以 1024 采样点 (21.33ms) 极低时延无损流式输入，纯硬件时钟对齐，零 CPU 空转
 * 3. 极速寄存器级 1024 点定点 (Q14) Radix-2 快速傅里叶变换 (FFT) + 汉宁窗 (Hanning Window)
 *    低频分辨率高达 46.88 Hz，真正区分 Sub-Bass (40~100Hz)、Kick 鼓点 (100~200Hz)、
 *    人声中频 (500~2.5kHz) 与高频打击乐 (2.5k~16kHz)
 * 4. 8 频段独立自适应增益控制 (AGC): 瞬时 Attack 抓拍 + 柔和 Decay 防闪烁
 * 5. I2C 差量 Deadband 智能滤波: 过滤肉眼不可见的微弱电平抖动，彻底解除 I2C 总线争用，CPU 降至极致
 * 6. 两种专业声学可视化模式:
 *    - 模式 1: 双翼 8 频段真·声学均衡器 (LED 1~8 与 17~10 对称映射 8 大声学频段)
 *    - 模式 2: 重低音展开大动态律动 + 频带质心动态流光 + DAW Peak-Hold 峰值悬停
 * 7. 数字静音门限 (Noise Gate): 停止播放时能量骤降立即平息，自动微光待机，杜绝误触发
 * ============================================================================== */

typedef unsigned long size_t;
typedef unsigned int uint32_t;
typedef unsigned short uint16_t;
typedef unsigned char uint8_t;
typedef unsigned long uint64_t;

/* ---------------- Linux aarch64 直通系统调用定义 ---------------- */
#define SYS_dup3          24
#define SYS_openat        56
#define SYS_close         57
#define SYS_pipe2         59
#define SYS_read          63
#define SYS_write         64
#define SYS_pread64       67
#define SYS_exit          93
#define SYS_nanosleep    101
#define SYS_clock_gettime 113
#define SYS_kill         129
#define SYS_clone        220
#define SYS_execve       221
#define SYS_wait4        260

#define AT_FDCWD        -100
#define O_RDONLY        0
#define O_WRONLY        1
#define O_RDWR          2
#define O_CREAT         0100
#define O_TRUNC         01000
#define SIGCHLD         17

#define CLOCK_MONOTONIC 1

struct timespec {
    long tv_sec;
    long tv_nsec;
};

static inline long sys_openat(int dfd, const char *filename, int flags, int mode) {
    register long x8 __asm__("x8") = SYS_openat;
    register long x0 __asm__("x0") = dfd;
    register const char *x1 __asm__("x1") = filename;
    register long x2 __asm__("x2") = flags;
    register long x3 __asm__("x3") = mode;
    __asm__ __volatile__("svc #0" : "+r"(x0) : "r"(x8), "r"(x1), "r"(x2), "r"(x3) : "memory");
    return x0;
}

static inline long sys_close(int fd) {
    register long x8 __asm__("x8") = SYS_close;
    register long x0 __asm__("x0") = fd;
    __asm__ __volatile__("svc #0" : "+r"(x0) : "r"(x8) : "memory");
    return x0;
}

static inline long sys_read(int fd, void *buf, size_t count) {
    register long x8 __asm__("x8") = SYS_read;
    register long x0 __asm__("x0") = fd;
    register void *x1 __asm__("x1") = buf;
    register long x2 __asm__("x2") = count;
    __asm__ __volatile__("svc #0" : "+r"(x0) : "r"(x8), "r"(x1), "r"(x2) : "memory");
    return x0;
}

static inline long sys_pread64(int fd, void *buf, size_t count, long pos) {
    register long x8 __asm__("x8") = SYS_pread64;
    register long x0 __asm__("x0") = fd;
    register void *x1 __asm__("x1") = buf;
    register long x2 __asm__("x2") = count;
    register long x3 __asm__("x3") = pos;
    __asm__ __volatile__("svc #0" : "+r"(x0) : "r"(x8), "r"(x1), "r"(x2), "r"(x3) : "memory");
    return x0;
}

static inline long sys_write(int fd, const void *buf, size_t count) {
    register long x8 __asm__("x8") = SYS_write;
    register long x0 __asm__("x0") = fd;
    register const void *x1 __asm__("x1") = buf;
    register long x2 __asm__("x2") = count;
    __asm__ __volatile__("svc #0" : "+r"(x0) : "r"(x8), "r"(x1), "r"(x2) : "memory");
    return x0;
}

static inline void sys_exit(int code) {
    register long x8 __asm__("x8") = SYS_exit;
    register long x0 __asm__("x0") = code;
    __asm__ __volatile__("svc #0" : : "r"(x8), "r"(x0) : "memory");
    while (1);
}

static inline long __attribute__((unused)) sys_clock_gettime(int clk_id, struct timespec *tp) {
    register long x8 __asm__("x8") = SYS_clock_gettime;
    register long x0 __asm__("x0") = clk_id;
    register void *x1 __asm__("x1") = tp;
    __asm__ __volatile__("svc #0" : "+r"(x0) : "r"(x8), "r"(x1) : "memory");
    return x0;
}

static inline long sys_nanosleep(const struct timespec *req, struct timespec *rem) {
    register long x8 __asm__("x8") = SYS_nanosleep;
    register long x0 __asm__("x0") = (long)req;
    register void *x1 __asm__("x1") = rem;
    __asm__ __volatile__("svc #0" : "+r"(x0) : "r"(x8), "r"(x1) : "memory");
    return x0;
}

static inline long sys_pipe2(int pipefd[2], int flags) {
    register long x8 __asm__("x8") = SYS_pipe2;
    register void *x0 __asm__("x0") = pipefd;
    register long x1 __asm__("x1") = flags;
    __asm__ __volatile__("svc #0" : "+r"(x0) : "r"(x8), "r"(x1) : "memory");
    return (long)x0;
}

static inline long sys_clone(unsigned long flags, void *child_stack) {
    register long x8 __asm__("x8") = SYS_clone;
    register unsigned long x0 __asm__("x0") = flags;
    register void *x1 __asm__("x1") = child_stack;
    register void *x2 __asm__("x2") = 0;
    register void *x3 __asm__("x3") = 0;
    register void *x4 __asm__("x4") = 0;
    __asm__ __volatile__("svc #0" : "+r"(x0) : "r"(x8), "r"(x1), "r"(x2), "r"(x3), "r"(x4) : "memory");
    return (long)x0;
}

static inline long sys_dup3(int oldfd, int newfd, int flags) {
    register long x8 __asm__("x8") = SYS_dup3;
    register long x0 __asm__("x0") = oldfd;
    register long x1 __asm__("x1") = newfd;
    register long x2 __asm__("x2") = flags;
    __asm__ __volatile__("svc #0" : "+r"(x0) : "r"(x8), "r"(x1), "r"(x2) : "memory");
    return (long)x0;
}

static inline long sys_execve(const char *path, char *const argv[], char *const envp[]) {
    register long x8 __asm__("x8") = SYS_execve;
    register const char *x0 __asm__("x0") = path;
    register void *x1 __asm__("x1") = (void *)argv;
    register void *x2 __asm__("x2") = (void *)envp;
    __asm__ __volatile__("svc #0" : "+r"(x0) : "r"(x8), "r"(x1), "r"(x2) : "memory");
    return (long)x0;
}

/* 基础内存操作 */
void *memcpy(void *dest, const void *src, size_t n) {
    char *d = (char *)dest;
    const char *s = (const char *)src;
    while (n--) *d++ = *s++;
    return dest;
}

void *memset(void *s, int c, size_t n) {
    char *p = (char *)s;
    while (n--) *p++ = (char)c;
    return s;
}

/* 快速整数平方根 */
static inline uint32_t int_sqrt(uint64_t val) {
    if (val == 0) return 0;
    uint64_t x = val;
    uint64_t y = (x + 1) >> 1;
    while (y < x) {
        x = y;
        y = (x + val / x) >> 1;
    }
    return (uint32_t)x;
}

/* ---------------- 硬件路径定义 ---------------- */
static const char PATH_LED_RGB[]    = "/sys/devices/i2c-0/0-003a/led_rgb";
static const char PATH_LED_FADE[]   = "/sys/devices/i2c-0/0-003a/led_fade";
static const char PATH_MUTE[]       = "/tmp/mipns/mute";
static const char PATH_MODE_LOG[]   = "/tmp/visualizer_mode";

/* ================================================================
 * 1024 点定点 FFT (Q14) 旋转因子与汉宁窗查表
 * ================================================================ */
#define FFT_N 1024
#define FFT_LOG2N 10

static const short cos_tbl[512] = {
     16384,  16384,  16383,  16381,  16379,  16376,  16373,  16369,  16364,  16359,  16353,  16347,  16340,  16332,  16324,  16315,
     16305,  16295,  16284,  16273,  16261,  16248,  16235,  16221,  16207,  16192,  16176,  16160,  16143,  16125,  16107,  16088,
     16069,  16049,  16029,  16008,  15986,  15964,  15941,  15917,  15893,  15868,  15843,  15817,  15791,  15763,  15736,  15707,
     15679,  15649,  15619,  15588,  15557,  15525,  15493,  15460,  15426,  15392,  15357,  15322,  15286,  15250,  15213,  15175,
     15137,  15098,  15059,  15019,  14978,  14937,  14896,  14854,  14811,  14768,  14724,  14680,  14635,  14589,  14543,  14497,
     14449,  14402,  14354,  14305,  14256,  14206,  14155,  14104,  14053,  14001,  13949,  13896,  13842,  13788,  13733,  13678,
     13623,  13567,  13510,  13453,  13395,  13337,  13279,  13219,  13160,  13100,  13039,  12978,  12916,  12854,  12792,  12729,
     12665,  12601,  12537,  12472,  12406,  12340,  12274,  12207,  12140,  12072,  12004,  11935,  11866,  11797,  11727,  11656,
     11585,  11514,  11442,  11370,  11297,  11224,  11151,  11077,  11003,  10928,  10853,  10778,  10702,  10625,  10549,  10471,
     10394,  10316,  10238,  10159,  10080,  10001,   9921,   9841,   9760,   9679,   9598,   9516,   9434,   9352,   9269,   9186,
      9102,   9019,   8935,   8850,   8765,   8680,   8595,   8509,   8423,   8337,   8250,   8163,   8076,   7988,   7900,   7812,
      7723,   7635,   7545,   7456,   7366,   7276,   7186,   7096,   7005,   6914,   6823,   6731,   6639,   6547,   6455,   6363,
      6270,   6177,   6084,   5990,   5897,   5803,   5708,   5614,   5520,   5425,   5330,   5235,   5139,   5044,   4948,   4852,
      4756,   4660,   4563,   4467,   4370,   4273,   4176,   4078,   3981,   3883,   3786,   3688,   3590,   3492,   3393,   3295,
      3196,   3098,   2999,   2900,   2801,   2702,   2603,   2503,   2404,   2305,   2205,   2105,   2006,   1906,   1806,   1706,
      1606,   1506,   1406,   1306,   1205,   1105,   1005,    904,    804,    704,    603,    503,    402,    302,    201,    101,
         0,   -101,   -201,   -302,   -402,   -503,   -603,   -704,   -804,   -904,  -1005,  -1105,  -1205,  -1306,  -1406,  -1506,
     -1606,  -1706,  -1806,  -1906,  -2006,  -2105,  -2205,  -2305,  -2404,  -2503,  -2603,  -2702,  -2801,  -2900,  -2999,  -3098,
     -3196,  -3295,  -3393,  -3492,  -3590,  -3688,  -3786,  -3883,  -3981,  -4078,  -4176,  -4273,  -4370,  -4467,  -4563,  -4660,
     -4756,  -4852,  -4948,  -5044,  -5139,  -5235,  -5330,  -5425,  -5520,  -5614,  -5708,  -5803,  -5897,  -5990,  -6084,  -6177,
     -6270,  -6363,  -6455,  -6547,  -6639,  -6731,  -6823,  -6914,  -7005,  -7096,  -7186,  -7276,  -7366,  -7456,  -7545,  -7635,
     -7723,  -7812,  -7900,  -7988,  -8076,  -8163,  -8250,  -8337,  -8423,  -8509,  -8595,  -8680,  -8765,  -8850,  -8935,  -9019,
     -9102,  -9186,  -9269,  -9352,  -9434,  -9516,  -9598,  -9679,  -9760,  -9841,  -9921, -10001, -10080, -10159, -10238, -10316,
    -10394, -10471, -10549, -10625, -10702, -10778, -10853, -10928, -11003, -11077, -11151, -11224, -11297, -11370, -11442, -11514,
    -11585, -11656, -11727, -11797, -11866, -11935, -12004, -12072, -12140, -12207, -12274, -12340, -12406, -12472, -12537, -12601,
    -12665, -12729, -12792, -12854, -12916, -12978, -13039, -13100, -13160, -13219, -13279, -13337, -13395, -13453, -13510, -13567,
    -13623, -13678, -13733, -13788, -13842, -13896, -13949, -14001, -14053, -14104, -14155, -14206, -14256, -14305, -14354, -14402,
    -14449, -14497, -14543, -14589, -14635, -14680, -14724, -14768, -14811, -14854, -14896, -14937, -14978, -15019, -15059, -15098,
    -15137, -15175, -15213, -15250, -15286, -15322, -15357, -15392, -15426, -15460, -15493, -15525, -15557, -15588, -15619, -15649,
    -15679, -15707, -15736, -15763, -15791, -15817, -15843, -15868, -15893, -15917, -15941, -15964, -15986, -16008, -16029, -16049,
    -16069, -16088, -16107, -16125, -16143, -16160, -16176, -16192, -16207, -16221, -16235, -16248, -16261, -16273, -16284, -16295,
    -16305, -16315, -16324, -16332, -16340, -16347, -16353, -16359, -16364, -16369, -16373, -16376, -16379, -16381, -16383, -16384,
};

static const short sin_tbl[512] = {
         0,    101,    201,    302,    402,    503,    603,    704,    804,    904,   1005,   1105,   1205,   1306,   1406,   1506,
      1606,   1706,   1806,   1906,   2006,   2105,   2205,   2305,   2404,   2503,   2603,   2702,   2801,   2900,   2999,   3098,
      3196,   3295,   3393,   3492,   3590,   3688,   3786,   3883,   3981,   4078,   4176,   4273,   4370,   4467,   4563,   4660,
      4756,   4852,   4948,   5044,   5139,   5235,   5330,   5425,   5520,   5614,   5708,   5803,   5897,   5990,   6084,   6177,
      6270,   6363,   6455,   6547,   6639,   6731,   6823,   6914,   7005,   7096,   7186,   7276,   7366,   7456,   7545,   7635,
      7723,   7812,   7900,   7988,   8076,   8163,   8250,   8337,   8423,   8509,   8595,   8680,   8765,   8850,   8935,   9019,
      9102,   9186,   9269,   9352,   9434,   9516,   9598,   9679,   9760,   9841,   9921,  10001,  10080,  10159,  10238,  10316,
     10394,  10471,  10549,  10625,  10702,  10778,  10853,  10928,  11003,  11077,  11151,  11224,  11297,  11370,  11442,  11514,
     11585,  11656,  11727,  11797,  11866,  11935,  12004,  12072,  12140,  12207,  12274,  12340,  12406,  12472,  12537,  12601,
     12665,  12729,  12792,  12854,  12916,  12978,  13039,  13100,  13160,  13219,  13279,  13337,  13395,  13453,  13510,  13567,
     13623,  13678,  13733,  13788,  13842,  13896,  13949,  14001,  14053,  14104,  14155,  14206,  14256,  14305,  14354,  14402,
     14449,  14497,  14543,  14589,  14635,  14680,  14724,  14768,  14811,  14854,  14896,  14937,  14978,  15019,  15059,  15098,
     15137,  15175,  15213,  15250,  15286,  15322,  15357,  15392,  15426,  15460,  15493,  15525,  15557,  15588,  15619,  15649,
     15679,  15707,  15736,  15763,  15791,  15817,  15843,  15868,  15893,  15917,  15941,  15964,  15986,  16008,  16029,  16049,
     16069,  16088,  16107,  16125,  16143,  16160,  16176,  16192,  16207,  16221,  16235,  16248,  16261,  16273,  16284,  16295,
     16305,  16315,  16324,  16332,  16340,  16347,  16353,  16359,  16364,  16369,  16373,  16376,  16379,  16381,  16383,  16384,
     16384,  16384,  16383,  16381,  16379,  16376,  16373,  16369,  16364,  16359,  16353,  16347,  16340,  16332,  16324,  16315,
     16305,  16295,  16284,  16273,  16261,  16248,  16235,  16221,  16207,  16192,  16176,  16160,  16143,  16125,  16107,  16088,
     16069,  16049,  16029,  16008,  15986,  15964,  15941,  15917,  15893,  15868,  15843,  15817,  15791,  15763,  15736,  15707,
     15679,  15649,  15619,  15588,  15557,  15525,  15493,  15460,  15426,  15392,  15357,  15322,  15286,  15250,  15213,  15175,
     15137,  15098,  15059,  15019,  14978,  14937,  14896,  14854,  14811,  14768,  14724,  14680,  14635,  14589,  14543,  14497,
     14449,  14402,  14354,  14305,  14256,  14206,  14155,  14104,  14053,  14001,  13949,  13896,  13842,  13788,  13733,  13678,
     13623,  13567,  13510,  13453,  13395,  13337,  13279,  13219,  13160,  13100,  13039,  12978,  12916,  12854,  12792,  12729,
     12665,  12601,  12537,  12472,  12406,  12340,  12274,  12207,  12140,  12072,  12004,  11935,  11866,  11797,  11727,  11656,
     11585,  11514,  11442,  11370,  11297,  11224,  11151,  11077,  11003,  10928,  10853,  10778,  10702,  10625,  10549,  10471,
     10394,  10316,  10238,  10159,  10080,  10001,   9921,   9841,   9760,   9679,   9598,   9516,   9434,   9352,   9269,   9186,
      9102,   9019,   8935,   8850,   8765,   8680,   8595,   8509,   8423,   8337,   8250,   8163,   8076,   7988,   7900,   7812,
      7723,   7635,   7545,   7456,   7366,   7276,   7186,   7096,   7005,   6914,   6823,   6731,   6639,   6547,   6455,   6363,
      6270,   6177,   6084,   5990,   5897,   5803,   5708,   5614,   5520,   5425,   5330,   5235,   5139,   5044,   4948,   4852,
      4756,   4660,   4563,   4467,   4370,   4273,   4176,   4078,   3981,   3883,   3786,   3688,   3590,   3492,   3393,   3295,
      3196,   3098,   2999,   2900,   2801,   2702,   2603,   2503,   2404,   2305,   2205,   2105,   2006,   1906,   1806,   1706,
      1606,   1506,   1406,   1306,   1205,   1105,   1005,    904,    804,    704,    603,    503,    402,    302,    201,    101,
};

static const short hann_tbl[1024] = {
         0,      0,      1,      1,      2,      4,      6,      8,     10,     13,     15,     19,     22,     26,     30,     35,
        40,     45,     50,     56,     62,     68,     75,     82,     89,     96,    104,    112,    121,    130,    139,    148,
       158,    168,    178,    189,    199,    211,    222,    234,    246,    258,    271,    284,    297,    311,    325,    339,
       353,    368,    383,    399,    414,    430,    446,    463,    480,    497,    514,    532,    550,    568,    587,    606,
       625,    644,    664,    684,    704,    725,    746,    767,    788,    810,    832,    854,    876,    899,    922,    946,
       969,    993,   1017,   1042,   1066,   1091,   1116,   1142,   1168,   1194,   1220,   1247,   1273,   1300,   1328,   1355,
      1383,   1411,   1440,   1468,   1497,   1526,   1556,   1585,   1615,   1645,   1676,   1706,   1737,   1768,   1800,   1831,
      1863,   1895,   1927,   1960,   1993,   2026,   2059,   2092,   2126,   2160,   2194,   2229,   2263,   2298,   2333,   2368,
      2404,   2440,   2475,   2512,   2548,   2585,   2621,   2658,   2696,   2733,   2771,   2808,   2846,   2885,   2923,   2962,
      3001,   3040,   3079,   3118,   3158,   3198,   3238,   3278,   3318,   3359,   3399,   3440,   3481,   3523,   3564,   3606,
      3647,   3689,   3731,   3774,   3816,   3859,   3902,   3944,   3988,   4031,   4074,   4118,   4162,   4205,   4249,   4294,
      4338,   4382,   4427,   4472,   4517,   4562,   4607,   4652,   4698,   4743,   4789,   4835,   4881,   4927,   4973,   5019,
      5066,   5112,   5159,   5206,   5253,   5300,   5347,   5394,   5441,   5489,   5536,   5584,   5632,   5680,   5728,   5776,
      5824,   5872,   5920,   5969,   6017,   6066,   6114,   6163,   6212,   6261,   6310,   6359,   6408,   6457,   6506,   6555,
      6605,   6654,   6703,   6753,   6803,   6852,   6902,   6952,   7001,   7051,   7101,   7151,   7201,   7251,   7301,   7351,
      7401,   7451,   7501,   7551,   7601,   7652,   7702,   7752,   7802,   7852,   7903,   7953,   8003,   8054,   8104,   8154,
      8205,   8255,   8305,   8356,   8406,   8456,   8506,   8557,   8607,   8657,   8707,   8758,   8808,   8858,   8908,   8958,
      9008,   9058,   9108,   9158,   9208,   9258,   9308,   9358,   9408,   9457,   9507,   9557,   9606,   9656,   9705,   9755,
      9804,   9853,   9903,   9952,  10001,  10050,  10099,  10148,  10197,  10245,  10294,  10343,  10391,  10440,  10488,  10536,
     10584,  10632,  10680,  10728,  10776,  10824,  10871,  10919,  10966,  11014,  11061,  11108,  11155,  11202,  11248,  11295,
     11341,  11388,  11434,  11480,  11526,  11572,  11618,  11664,  11709,  11754,  11800,  11845,  11890,  11935,  11979,  12024,
     12068,  12112,  12157,  12201,  12244,  12288,  12331,  12375,  12418,  12461,  12504,  12547,  12589,  12632,  12674,  12716,
     12758,  12799,  12841,  12882,  12923,  12964,  13005,  13046,  13086,  13126,  13166,  13206,  13246,  13286,  13325,  13364,
     13403,  13442,  13480,  13518,  13557,  13595,  13632,  13670,  13707,  13744,  13781,  13818,  13854,  13890,  13927,  13962,
     13998,  14033,  14068,  14103,  14138,  14173,  14207,  14241,  14275,  14308,  14342,  14375,  14408,  14440,  14473,  14505,
     14537,  14569,  14600,  14631,  14662,  14693,  14724,  14754,  14784,  14814,  14843,  14872,  14901,  14930,  14959,  14987,
     15015,  15042,  15070,  15097,  15124,  15151,  15177,  15203,  15229,  15255,  15280,  15305,  15330,  15355,  15379,  15403,
     15427,  15450,  15473,  15496,  15519,  15541,  15563,  15585,  15607,  15628,  15649,  15670,  15690,  15710,  15730,  15750,
     15769,  15788,  15807,  15825,  15843,  15861,  15878,  15896,  15913,  15929,  15946,  15962,  15978,  15993,  16008,  16023,
     16038,  16052,  16066,  16080,  16093,  16106,  16119,  16132,  16144,  16156,  16168,  16179,  16190,  16201,  16211,  16221,
     16231,  16241,  16250,  16259,  16267,  16276,  16284,  16291,  16299,  16306,  16313,  16319,  16325,  16331,  16337,  16342,
     16347,  16352,  16356,  16360,  16364,  16367,  16370,  16373,  16375,  16377,  16379,  16381,  16382,  16383,  16384,  16384,
     16384,  16384,  16383,  16382,  16381,  16379,  16377,  16375,  16373,  16370,  16367,  16364,  16360,  16356,  16352,  16347,
     16342,  16337,  16331,  16325,  16319,  16313,  16306,  16299,  16291,  16284,  16276,  16267,  16259,  16250,  16241,  16231,
     16221,  16211,  16201,  16190,  16179,  16168,  16156,  16144,  16132,  16119,  16106,  16093,  16080,  16066,  16052,  16038,
     16023,  16008,  15993,  15978,  15962,  15946,  15929,  15913,  15896,  15878,  15861,  15843,  15825,  15807,  15788,  15769,
     15750,  15730,  15710,  15690,  15670,  15649,  15628,  15607,  15585,  15563,  15541,  15519,  15496,  15473,  15450,  15427,
     15403,  15379,  15355,  15330,  15305,  15280,  15255,  15229,  15203,  15177,  15151,  15124,  15097,  15070,  15042,  15015,
     14987,  14959,  14930,  14901,  14872,  14843,  14814,  14784,  14754,  14724,  14693,  14662,  14631,  14600,  14569,  14537,
     14505,  14473,  14440,  14408,  14375,  14342,  14308,  14275,  14241,  14207,  14173,  14138,  14103,  14068,  14033,  13998,
     13962,  13927,  13890,  13854,  13818,  13781,  13744,  13707,  13670,  13632,  13595,  13557,  13518,  13480,  13442,  13403,
     13364,  13325,  13286,  13246,  13206,  13166,  13126,  13086,  13046,  13005,  12964,  12923,  12882,  12841,  12799,  12758,
     12716,  12674,  12632,  12589,  12547,  12504,  12461,  12418,  12375,  12331,  12288,  12244,  12201,  12157,  12112,  12068,
     12024,  11979,  11935,  11890,  11845,  11800,  11754,  11709,  11664,  11618,  11572,  11526,  11480,  11434,  11388,  11341,
     11295,  11248,  11202,  11155,  11108,  11061,  11014,  10966,  10919,  10871,  10824,  10776,  10728,  10680,  10632,  10584,
     10536,  10488,  10440,  10391,  10343,  10294,  10245,  10197,  10148,  10099,  10050,  10001,   9952,   9903,   9853,   9804,
      9755,   9705,   9656,   9606,   9557,   9507,   9457,   9408,   9358,   9308,   9258,   9208,   9158,   9108,   9058,   9008,
      8958,   8908,   8858,   8808,   8758,   8707,   8657,   8607,   8557,   8506,   8456,   8406,   8356,   8305,   8255,   8205,
      8154,   8104,   8054,   8003,   7953,   7903,   7852,   7802,   7752,   7702,   7652,   7601,   7551,   7501,   7451,   7401,
      7351,   7301,   7251,   7201,   7151,   7101,   7051,   7001,   6952,   6902,   6852,   6803,   6753,   6703,   6654,   6605,
      6555,   6506,   6457,   6408,   6359,   6310,   6261,   6212,   6163,   6114,   6066,   6017,   5969,   5920,   5872,   5824,
      5776,   5728,   5680,   5632,   5584,   5536,   5489,   5441,   5394,   5347,   5300,   5253,   5206,   5159,   5112,   5066,
      5019,   4973,   4927,   4881,   4835,   4789,   4743,   4698,   4652,   4607,   4562,   4517,   4472,   4427,   4382,   4338,
      4294,   4249,   4205,   4162,   4118,   4074,   4031,   3988,   3944,   3902,   3859,   3816,   3774,   3731,   3689,   3647,
      3606,   3564,   3523,   3481,   3440,   3399,   3359,   3318,   3278,   3238,   3198,   3158,   3118,   3079,   3040,   3001,
      2962,   2923,   2885,   2846,   2808,   2771,   2733,   2696,   2658,   2621,   2585,   2548,   2512,   2475,   2440,   2404,
      2368,   2333,   2298,   2263,   2229,   2194,   2160,   2126,   2092,   2059,   2026,   1993,   1960,   1927,   1895,   1863,
      1831,   1800,   1768,   1737,   1706,   1676,   1645,   1615,   1585,   1556,   1526,   1497,   1468,   1440,   1411,   1383,
      1355,   1328,   1300,   1273,   1247,   1220,   1194,   1168,   1142,   1116,   1091,   1066,   1042,   1017,    993,    969,
       946,    922,    899,    876,    854,    832,    810,    788,    767,    746,    725,    704,    684,    664,    644,    625,
       606,    587,    568,    550,    532,    514,    497,    480,    463,    446,    430,    414,    399,    383,    368,    353,
       339,    325,    311,    297,    284,    271,    258,    246,    234,    222,    211,    199,    189,    178,    168,    158,
       148,    139,    130,    121,    112,    104,     96,     89,     82,     75,     68,     62,     56,     50,     45,     40,
        35,     30,     26,     22,     19,     15,     13,     10,      8,      6,      4,      2,      1,      1,      0,      0,
};

/* FFT 工作缓冲区 */
static int xr[FFT_N];
static int xi[FFT_N];

/* 位反转快速映射 */
static inline int bit_reverse(int i) {
    int r = 0;
    for (int b = 0; b < FFT_LOG2N; b++) {
        r = (r << 1) | (i & 1);
        i >>= 1;
    }
    return r;
}

/* 1024 点 Radix-2 定点 FFT 极速计算函数 (寄存器复用优化版) */
static void fft_1024(const short *in_samples, int *out_mags) {
    for (int i = 0; i < FFT_N; i++) {
        int rev = bit_reverse(i);
        xr[rev] = ((long)in_samples[i] * hann_tbl[i]) >> 14;
        xi[rev] = 0;
    }

    int step = 2;
    while (step <= FFT_N) {
        int half = step >> 1;
        int twiddle_step = FFT_N / step;
        int tw_idx = 0;
        for (int j = 0; j < half; j++) {
            int wr = cos_tbl[tw_idx];
            int wi = sin_tbl[tw_idx];

            for (int k = j; k < FFT_N; k += step) {
                int qr = xr[k + half];
                int qi = xi[k + half];

                int tr = (int)(((long)qr * wr + (long)qi * wi) >> 14);
                int ti = (int)(((long)qi * wr - (long)qr * wi) >> 14);

                int pr = xr[k];
                int pi = xi[k];

                xr[k] = pr + tr;
                xi[k] = pi + ti;
                xr[k + half] = pr - tr;
                xi[k + half] = pi - ti;
            }
            tw_idx += twiddle_step;
        }
        step <<= 1;
    }

    /* 前 512 个频点的幅值近似 (Alpha max + Beta min) */
    for (int i = 0; i < (FFT_N >> 1); i++) {
        int r = xr[i]; if (r < 0) r = -r;
        int im = xi[i]; if (im < 0) im = -im;
        out_mags[i] = (r > im) ? (r + (im >> 1)) : (im + (r >> 1));
    }
}

/* ================================================================
 * 8 大关键声学频段映射定义 (48kHz 采样率, 46.88 Hz/bin)
 * ================================================================ */
static const struct {
    int start_bin;
    int end_bin;
} BANDS[8] = {
    { 1,   2   }, /* Band 0: ~47~94 Hz   (深潜超低音 Sub-Bass: 大鼓下潜, 808 根音) */
    { 3,   4   }, /* Band 1: ~141~188 Hz (强劲低音 Bass Punch: 底鼓打击力, 贝斯弹拨) */
    { 5,   9   }, /* Band 2: ~234~422 Hz (温暖中低频 Low Mids: 军鼓鼓身, 男声下潜) */
    { 10,  20  }, /* Band 3: ~469~938 Hz (核心中频 Midrange: 主唱人声, 旋律键盘) */
    { 21,  46  }, /* Band 4: ~984~2156 Hz (中高泛音 High Mids: 人声咬字, 铜管打击) */
    { 47,  95  }, /* Band 5: ~2.2k~4.5kHz (存在感 Presence: 吉他失真, 瞬态敲击) */
    { 96,  180 }, /* Band 6: ~4.5k~8.4kHz (明亮高频 Treble: 踩镲, 砂槌, 军鼓高频) */
    { 181, 340 }  /* Band 7: ~8.5k~15.9kHz (极高频 Air: 吊镲空气感, 电子音效) */
};

static inline int get_band_energy(const int *mags, int b) {
    int start = BANDS[b].start_bin;
    int end = BANDS[b].end_bin;
    int max_val = 0;
    for (int i = start; i <= end; i++) {
        if (mags[i] > max_val) max_val = mags[i];
    }
    return max_val;
}

/* ================================================================
 * 高保真色彩系统 (BGR 格式: 0xBBGGRR)
 * ================================================================ */
#define C_BLACK     0x000000
#define C_BASE      0x100002    /* 极暗深紫微光底色 */
#define C_WHITE     0xFFFFFF    /* 爆闪极光纯白 */
#define C_MUTE_RED  0x0000FF    /* 麦克风静音正红 */

/* 8 频段光谱调色板 (从低频炽热烈焰 → 中频生机翠绿 → 高频赛博冰蓝) */
static const uint32_t PAL_SPECTRUM[8] = {
    0x0000FF,   /* Band 0: 烈焰深红 (Sub-Bass) */
    0x0045FF,   /* Band 1: 烈阳火橙 (Bass Punch) */
    0x00B5FF,   /* Band 2: 琥珀金黄 (Low Mids) */
    0x00FF30,   /* Band 3: 荧光青翠 (Midrange) */
    0xFFFF00,   /* Band 4: 赛博明青 (High Mids) */
    0xFF7500,   /* Band 5: 天空湛蓝 (Presence) */
    0xFF0050,   /* Band 6: 极电靛蓝 (Treble) */
    0xFF40FF    /* Band 7: 极光粉紫 (Air) */
};

/* 颜色缩放与混合算法 */
static inline uint32_t scale_color(uint32_t c, int level) {
    if (level <= 0) return C_BLACK;
    if (level >= 100) return c;
    /* 量化到步长 2，抑制微小电平抖动 */
    int q_lev = (level / 2) * 2;
    uint32_t b = (c >> 16) & 0xFF;
    uint32_t g = (c >> 8) & 0xFF;
    uint32_t r = c & 0xFF;
    b = (b * q_lev) / 100;
    g = (g * q_lev) / 100;
    r = (r * q_lev) / 100;
    return (b << 16) | (g << 8) | r;
}

static inline uint32_t blend_color(uint32_t c1, uint32_t c2, int t) {
    if (t <= 0) return c1;
    if (t >= 100) return c2;
    uint32_t b1 = (c1 >> 16) & 0xFF, b2 = (c2 >> 16) & 0xFF;
    uint32_t g1 = (c1 >> 8) & 0xFF,  g2 = (c2 >> 8) & 0xFF;
    uint32_t r1 = c1 & 0xFF,         r2 = c2 & 0xFF;
    uint32_t b = (b1 * (100 - t) + b2 * t) / 100;
    uint32_t g = (g1 * (100 - t) + g2 * t) / 100;
    uint32_t r = (r1 * (100 - t) + r2 * t) / 100;
    return (b << 16) | (g << 8) | r;
}

/* 颜色差异计算 (Deadband 滤除人眼不可见微扰) */
static inline int color_diff(uint32_t c1, uint32_t c2) {
    int db = (int)((c1 >> 16) & 0xFF) - (int)((c2 >> 16) & 0xFF);
    int dg = (int)((c1 >> 8) & 0xFF)  - (int)((c2 >> 8) & 0xFF);
    int dr = (int)(c1 & 0xFF)         - (int)(c2 & 0xFF);
    if (db < 0) db = -db;
    if (dg < 0) dg = -dg;
    if (dr < 0) dr = -dr;
    return (db > dg ? (db > dr ? db : dr) : (dg > dr ? dg : dr));
}

/* 硬件显存缓存与差量提交 */
static uint32_t current_colors[18];
static uint32_t last_colors[18];

static inline int fmt_led_cmd(char *buf, int idx, uint32_t color) {
    char *p = buf;
    if (idx >= 10) {
        *p++ = '0' + (idx / 10);
        *p++ = '0' + (idx % 10);
    } else {
        *p++ = '0' + idx;
    }
    *p++ = ' ';
    *p++ = '0';
    *p++ = 'x';
    static const char hex[] = "0123456789ABCDEF";
    *p++ = hex[(color >> 20) & 0xF];
    *p++ = hex[(color >> 16) & 0xF];
    *p++ = hex[(color >> 12) & 0xF];
    *p++ = hex[(color >> 8) & 0xF];
    *p++ = hex[(color >> 4) & 0xF];
    *p++ = hex[color & 0xF];
    *p++ = '\n';
    *p = '\0';
    return (int)(p - buf);
}

static inline void flush_leds(int fd_led, int force) {
    char buf[32];
    for (int i = 0; i < 18; i++) {
        if (force || color_diff(current_colors[i], last_colors[i]) >= 6) {
            int len = fmt_led_cmd(buf, i, current_colors[i]);
            sys_write(fd_led, buf, len);
            last_colors[i] = current_colors[i];
        }
    }
}

static inline void set_all_leds(uint32_t color) {
    for (int i = 0; i < 18; i++) current_colors[i] = color;
}

static void log_mode(int mode) {
    int fd = sys_openat(AT_FDCWD, PATH_MODE_LOG, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0) {
        if (mode == 1) {
            static const char m[] = "模式 1: 双翼 8 频段真·声学均衡器 (纯硬件 FFT 驱动)\n";
            sys_write(fd, m, sizeof(m) - 1);
        } else {
            static const char m[] = "模式 2: 重低音大动态立体声律动 (动态色温 + 峰值悬停)\n";
            sys_write(fd, m, sizeof(m) - 1);
        }
        sys_close(fd);
    }
}

/* ================================================================
 * arecord 子进程硬件流捕获管理
 * ================================================================ */
static int spawn_arecord(void) {
    int pfd[2];
    if (sys_pipe2(pfd, 0) < 0) return -1;

    long pid = sys_clone(SIGCHLD, 0);
    if (pid < 0) {
        sys_close(pfd[0]);
        sys_close(pfd[1]);
        return -1;
    }

    if (pid == 0) {
        /* 子进程 arecord 执行体 */
        sys_close(pfd[0]);
        sys_dup3(pfd[1], 1, 0); /* stdout -> pipe */
        sys_close(pfd[1]);

        int devnull = sys_openat(AT_FDCWD, "/dev/null", O_WRONLY, 0);
        if (devnull >= 0) {
            sys_dup3(devnull, 2, 0); /* stderr -> /dev/null */
            sys_close(devnull);
        }

        char *const argv[] = {
            "/usr/bin/arecord",
            "-q",
            "-D", "hw:0,2",
            "-f", "S16_LE",
            "-r", "48000",
            "-c", "2",
            "-t", "raw",
            "--period-size=1024",
            "--buffer-size=4096",
            0
        };
        char *const envp[] = { 0 };
        sys_execve("/usr/bin/arecord", argv, envp);
        sys_exit(127);
    }

    /* 父进程保留读取端 */
    sys_close(pfd[1]);
    return pfd[0];
}

/* 双声道解包缓冲区 */
static short raw_interleaved[FFT_N * 2];
static short left_samples[FFT_N];
static short right_samples[FFT_N];
static int left_mags[FFT_N >> 1];
static int right_mags[FFT_N >> 1];

/* 状态枚举 */
#define STATE_STOPPED 0
#define STATE_PLAYING 1
#define STATE_PAUSED  2

/* ---------------- 核心主循环 ---------------- */
void main_loop(long argc, char **argv) {
    int current_mode = 1;
    int auto_cycle = 1;

    if (argc >= 2 && argv[1]) {
        if (argv[1][0] == '1') { current_mode = 1; auto_cycle = 0; }
        else if (argv[1][0] == '2') { current_mode = 2; auto_cycle = 0; }
        else if (argv[1][0] == 'a') { auto_cycle = 1; }
    }

    /* 1. 初始化 AW20054 渐变时间 */
    int fd_fade = sys_openat(AT_FDCWD, PATH_LED_FADE, O_WRONLY, 0);
    if (fd_fade >= 0) {
        static const char fr[] = "r 0xff\n";
        static const char fg[] = "g 0xff\n";
        static const char fb[] = "b 0xff\n";
        sys_write(fd_fade, fr, sizeof(fr) - 1);
        sys_write(fd_fade, fg, sizeof(fg) - 1);
        sys_write(fd_fade, fb, sizeof(fb) - 1);
        sys_close(fd_fade);
    }

    /* 2. 打开 LED 控制接口 */
    int fd_led = sys_openat(AT_FDCWD, PATH_LED_RGB, O_WRONLY, 0);
    for (int i = 0; i < 18; i++) {
        current_colors[i] = C_BLACK;
        last_colors[i] = 0xFFFFFFFF;
    }
    flush_leds(fd_led, 1);
    log_mode(current_mode);

    /* 3. 启动硬件 PCM 流 */
    int fd_audio = spawn_arecord();

    /* AGC 与平滑状态变量 (8 个频段分别独立追踪) */
    int high_l[8], low_l[8], level_l[8];
    int high_r[8], low_r[8], level_r[8];
    for (int b = 0; b < 8; b++) {
        high_l[b] = 8000; low_l[b] = 500; level_l[b] = 0;
        high_r[b] = 8000; low_r[b] = 500; level_r[b] = 0;
    }

    /* 模式 2 扩展变量 */
    int spread_l = 0, spread_r = 0;
    int peak_l = 0, peak_hold_l = 0;
    int peak_r = 0, peak_hold_r = 0;

    int state = STATE_STOPPED;
    int last_state = STATE_STOPPED;
    int silent_frame_count = 0;
    int mode_frame_counter = 0;
    int mute_check_counter = 0;

    while (1) {
        /* -------------------------------------------------------------
         * 1. 极低延迟音频流阻塞读取 (1024 对采样点 = 4096 字节 = 21.33ms)
         * ------------------------------------------------------------- */
        size_t needed = sizeof(raw_interleaved);
        size_t done = 0;
        char *buf_ptr = (char *)raw_interleaved;

        while (done < needed) {
            long r = sys_read(fd_audio, buf_ptr + done, needed - done);
            if (r <= 0) break;
            done += r;
        }

        if (done < needed) {
            /* 音频子进程异常或管道断开，重新拉起 */
            sys_close(fd_audio);
            struct timespec delay = {0, 100000000L};
            sys_nanosleep(&delay, 0);
            fd_audio = spawn_arecord();
            continue;
        }

        /* -------------------------------------------------------------
         * 2. 麦克风静音物理按键检测 (每 20 帧 ~ 400ms 一次)
         * ------------------------------------------------------------- */
        mute_check_counter++;
        if (mute_check_counter >= 20) {
            mute_check_counter = 0;
            int fd_mute = sys_openat(AT_FDCWD, PATH_MUTE, O_RDONLY, 0);
            if (fd_mute >= 0) {
                char mute_buf[8];
                long mn = sys_pread64(fd_mute, mute_buf, sizeof(mute_buf) - 1, 0);
                sys_close(fd_mute);
                if (mn > 0 && mute_buf[0] == '1') {
                    set_all_leds(C_MUTE_RED);
                    flush_leds(fd_led, 1);
                    struct timespec ms = {0, 100000000L};
                    sys_nanosleep(&ms, 0);
                    continue;
                }
            }
        }

        /* -------------------------------------------------------------
         * 3. 双声道解包与 RMS 全局能量监测
         * ------------------------------------------------------------- */
        uint64_t sum_sq = 0;
        for (int i = 0; i < FFT_N; i++) {
            short sl = raw_interleaved[i * 2];
            short sr = raw_interleaved[i * 2 + 1];
            left_samples[i] = sl;
            right_samples[i] = sr;
            sum_sq += (uint64_t)((long)sl * sl + (long)sr * sr);
        }
        uint32_t rms = int_sqrt(sum_sq / (FFT_N * 2));

        /* -------------------------------------------------------------
         * 4. 静音与播放状态机自动切换
         * ------------------------------------------------------------- */
        if (rms < 16) {
            silent_frame_count++;
            if (silent_frame_count > 90) { /* ~2 秒静音进入待机 */
                state = (silent_frame_count > 1400) ? STATE_STOPPED : STATE_PAUSED;
            }
        } else {
            silent_frame_count = 0;
            state = STATE_PLAYING;
        }

        if (state == STATE_STOPPED) {
            if (last_state != STATE_STOPPED) {
                set_all_leds(C_BLACK);
                flush_leds(fd_led, 1);
                last_state = STATE_STOPPED;
                for (int b = 0; b < 8; b++) {
                    level_l[b] = 0; level_r[b] = 0;
                }
                spread_l = 0; spread_r = 0;
            }
            continue;
        }

        if (state == STATE_PAUSED) {
            if (last_state != STATE_PAUSED) {
                set_all_leds(C_BASE);
                flush_leds(fd_led, 1);
                last_state = STATE_PAUSED;
            }
            for (int b = 0; b < 8; b++) {
                level_l[b] = (level_l[b] * 70) / 100;
                level_r[b] = (level_r[b] * 70) / 100;
            }
            if (spread_l > 0) spread_l--;
            if (spread_r > 0) spread_r--;
            continue;
        }

        last_state = STATE_PLAYING;

        /* -------------------------------------------------------------
         * 5. 左右声道独立执行 1024 点 FFT
         * ------------------------------------------------------------- */
        fft_1024(left_samples, left_mags);
        fft_1024(right_samples, right_mags);

        /* -------------------------------------------------------------
         * 6. 多频段 AGC 自适应包络提取 (Attack 瞬时, Decay 柔和)
         * ------------------------------------------------------------- */
        int total_bass = 0;
        int total_mid = 0;
        int total_treble = 0;

        for (int b = 0; b < 8; b++) {
            int el = get_band_energy(left_mags, b);
            int er = get_band_energy(right_mags, b);

            /* 左声道 AGC */
            if (el > high_l[b]) high_l[b] = el;
            else high_l[b] = (high_l[b] * 199 + el) / 200;

            if (el < low_l[b]) low_l[b] = el;
            else low_l[b] = (low_l[b] * 199 + el) / 200;

            int diff_l = high_l[b] - low_l[b];
            if (diff_l < 3000) diff_l = 3000;

            int raw_l = 0;
            if (el > low_l[b]) {
                raw_l = ((long)(el - low_l[b]) * 100) / diff_l;
                if (raw_l > 100) raw_l = 100;
            }

            if (raw_l >= level_l[b]) level_l[b] = raw_l;
            else level_l[b] = (level_l[b] * 88) / 100;

            /* 右声道 AGC */
            if (er > high_r[b]) high_r[b] = er;
            else high_r[b] = (high_r[b] * 199 + er) / 200;

            if (er < low_r[b]) low_r[b] = er;
            else low_r[b] = (low_r[b] * 199 + er) / 200;

            int diff_r = high_r[b] - low_r[b];
            if (diff_r < 3000) diff_r = 3000;

            int raw_r = 0;
            if (er > low_r[b]) {
                raw_r = ((long)(er - low_r[b]) * 100) / diff_r;
                if (raw_r > 100) raw_r = 100;
            }

            if (raw_r >= level_r[b]) level_r[b] = raw_r;
            else level_r[b] = (level_r[b] * 88) / 100;

            if (b < 2) total_bass += (level_l[b] + level_r[b]);
            else if (b < 5) total_mid += (level_l[b] + level_r[b]);
            else total_treble += (level_l[b] + level_r[b]);
        }

        /* 模式轮换计时 */
        if (auto_cycle) {
            mode_frame_counter++;
            if (mode_frame_counter >= 2800) { /* ~60秒轮换 */
                mode_frame_counter = 0;
                current_mode = (current_mode == 1) ? 2 : 1;
                log_mode(current_mode);
            }
        }

        /* -------------------------------------------------------------
         * 7. 渲染视觉灯效
         * ------------------------------------------------------------- */
        if (current_mode == 1) {
            /* ===========================================================
             * 模式 1: 双翼 8 频段真·声学频谱分析仪 (Equalizer)
             * 左翼: LED 1 到 8 映射左声道 8 个频段
             * 右翼: LED 17 到 10 映射右声道 8 个频段
             * LED 0: 顶部主鼓点强震 (Sub-Bass 爆发)
             * LED 9: 底部高频碰撞瞬态 (Treble / Clap 极光闪烁)
             * =========================================================== */

            /* 左翼 8 频段渲染 */
            for (int b = 0; b < 8; b++) {
                int led_idx = 1 + b;
                int lev = level_l[b];
                uint32_t col = PAL_SPECTRUM[b];
                if (lev > 85) {
                    col = blend_color(col, C_WHITE, (lev - 85) * 6);
                }
                current_colors[led_idx] = scale_color(col, lev);
            }

            /* 右翼 8 频段渲染 */
            for (int b = 0; b < 8; b++) {
                int led_idx = 17 - b;
                int lev = level_r[b];
                uint32_t col = PAL_SPECTRUM[b];
                if (lev > 85) {
                    col = blend_color(col, C_WHITE, (lev - 85) * 6);
                }
                current_colors[led_idx] = scale_color(col, lev);
            }

            /* 顶部 LED 0: 纯正低音鼓心跳 */
            int bass_energy = (level_l[0] + level_r[0]) / 2;
            if (bass_energy > 50) {
                uint32_t bass_pulse = blend_color(0x0020FF, C_WHITE, (bass_energy - 50) * 2);
                current_colors[0] = scale_color(bass_pulse, bass_energy);
            } else {
                current_colors[0] = scale_color(0x0000FF, bass_energy);
            }

            /* 底部 LED 9: 高频打击瞬态碰撞 */
            int treble_energy = (level_l[6] + level_r[6] + level_l[7] + level_r[7]) / 4;
            if (treble_energy > 60) {
                current_colors[9] = scale_color(C_WHITE, treble_energy);
            } else {
                current_colors[9] = scale_color(0xFF40FF, treble_energy);
            }

        } else {
            /* ===========================================================
             * 模式 2: 重低音大动态立体声律动 (动态色温 + 峰值悬停)
             * 重低音控制灯条延伸展翼长度 (从顶部 0 向下延伸至 8 与 10)
             * 全频段能量质心自适应控制流光色温 (火红 → 翠绿 → 赛博青蓝)
             * =========================================================== */
            int bass_avg_l = (level_l[0] * 2 + level_l[1]) / 3;
            int bass_avg_r = (level_r[0] * 2 + level_r[1]) / 3;

            int target_l = (bass_avg_l * 8) / 100;
            if (bass_avg_l > 5 && target_l == 0) target_l = 1;
            if (target_l > spread_l) spread_l = target_l;
            else if (target_l < spread_l) spread_l--;

            int target_r = (bass_avg_r * 8) / 100;
            if (bass_avg_r > 5 && target_r == 0) target_r = 1;
            if (target_r > spread_r) spread_r = target_r;
            else if (target_r < spread_r) spread_r--;

            /* Peak-Hold 峰值悬停 */
            if (spread_l > peak_l) { peak_l = spread_l; peak_hold_l = 10; }
            else if (peak_hold_l > 0) peak_hold_l--;
            else if (peak_l > 0) peak_l--;

            if (spread_r > peak_r) { peak_r = spread_r; peak_hold_r = 10; }
            else if (peak_hold_r > 0) peak_hold_r--;
            else if (peak_r > 0) peak_r--;

            /* 动态色温质心计算 */
            int sum_all = total_bass + total_mid + total_treble;
            uint32_t active_color;
            if (sum_all > 0) {
                int bass_ratio = (total_bass * 100) / sum_all;
                int treble_ratio = (total_treble * 100) / sum_all;
                if (bass_ratio > 50) {
                    active_color = blend_color(0x0000FF, 0x0080FF, (100 - bass_ratio) * 2);
                } else if (treble_ratio > 35) {
                    active_color = blend_color(0xFFFF00, 0xFF0080, treble_ratio * 2);
                } else {
                    active_color = blend_color(0x00FF40, 0x00B5FF, 50);
                }
            } else {
                active_color = 0x0055FF;
            }

            /* 渲染左翼 */
            for (int step = 1; step <= 8; step++) {
                if (step <= spread_l) {
                    current_colors[step] = active_color;
                } else if (step == peak_l && peak_l > spread_l && peak_l > 0) {
                    current_colors[step] = C_WHITE;
                } else {
                    current_colors[step] = C_BASE;
                }
            }

            /* 渲染右翼 */
            for (int step = 1; step <= 8; step++) {
                int idx = 18 - step;
                if (step <= spread_r) {
                    current_colors[idx] = active_color;
                } else if (step == peak_r && peak_r > spread_r && peak_r > 0) {
                    current_colors[idx] = C_WHITE;
                } else {
                    current_colors[idx] = C_BASE;
                }
            }

            current_colors[0] = active_color;
            if (spread_l >= 8 && spread_r >= 8) {
                current_colors[9] = C_WHITE;
            } else {
                current_colors[9] = C_BASE;
            }
        }

        /* 提交差量 I2C 硬件写入 (Deadband >= 6) */
        flush_leds(fd_led, 0);
    }
}

/* aarch64 纯汇编程序入口点 */
__asm__(
    ".global _start\n"
    "_start:\n"
    "    ldr x0, [sp]\n"       /* argc */
    "    add x1, sp, #8\n"     /* argv */
    "    b main_loop\n"
);
