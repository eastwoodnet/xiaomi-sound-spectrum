#!/bin/bash
# ==============================================================================
# 小米 Sound (L06A) 原生 C 语言律动服务交叉编译脚本
# 目标架构: ARM64 (aarch64-linux-gnu)
# 依赖工具: clang, lld, llvm-strip
# ==============================================================================

set -e

SRC_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SRC="${SRC_DIR}/led_music.c"
OUT="${SRC_DIR}/led_music"
EXTRA_FLAGS=()
case "${1:-l06a}" in
    l06a) ;;
    oh2p)
        OUT="${SRC_DIR}/led_music_oh2p"
        EXTRA_FLAGS+=(-DDEVICE_OH2P)
        ;;
    *) echo "Usage: $0 [l06a|oh2p]" >&2; exit 2 ;;
esac

echo "[*] 开始交叉编译 aarch64 原生二进制: ${OUT} ..."

clang -target aarch64-linux-gnu \
      -fuse-ld=lld \
      -nostdlib \
      -static \
      -fno-stack-protector \
      -fno-builtin \
      -O3 \
      "${EXTRA_FLAGS[@]}" \
      "${SRC}" -o "${OUT}"

if command -v llvm-strip >/dev/null 2>&1; then
    llvm-strip "${OUT}"
    echo "[+] 使用 llvm-strip 完成符号剥离。"
fi

echo "[+] 编译成功！文件大小: $(du -h "${OUT}" | cut -f1)"
file "${OUT}"
