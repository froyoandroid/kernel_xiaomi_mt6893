#!/bin/bash

# Date/Time (WIB = Indonesia Western Time, UTC+7)
SECONDS=0
DATE=$(TZ=Asia/Jakarta date '+%Y%m%d-%H%M')

# Device
DEVICE="chopin"
DEFCONFIG="chopin_defconfig"
KERNEL_NAME=$(grep "CONFIG_LOCALVERSION=" "arch/arm64/configs/${DEFCONFIG}" 2>/dev/null | cut -d'"' -f2 | sed 's/^-//')
KERNEL_NAME="${KERNEL_NAME:-Project-Hail_Mary}"
ZIPNAME="${KERNEL_NAME}-Chopin-${DATE}.zip"

echo -e "Building for: chopin\n"

# Toolchain: locate an LLVM/clang toolchain.
# Export TC_DIR to override; otherwise probe common roots for a clang-r* build.
SELF_DIR="$(cd "$(dirname "$0")" && pwd)"
if [ -n "$TC_DIR" ] && [ -x "$TC_DIR/bin/clang" ]; then
  TC_DIR="$(cd "$TC_DIR" && pwd)"
else
  TC_DIR=""
  for root in \
    "$SELF_DIR/../toolchains" \
    "$SELF_DIR/toolchains" \
    "$HOME/toolchains" \
    "/root/poco/toolchains" \
    "/root/toolchains"; do
    [ -d "$root" ] || continue
    # Prefer the pinned release, else fall back to the newest clang-r* present.
    if [ -x "$root/clang-r547379/bin/clang" ]; then
      TC_DIR="$root/clang-r547379"
      break
    fi
    for d in "$root"/clang-r*; do
      [ -x "$d/bin/clang" ] || continue
      TC_DIR="$d"
    done
    [ -n "$TC_DIR" ] && break
  done
fi

if [ -z "$TC_DIR" ]; then
  echo "Toolchain directory not found! Looked for clang-r* under:"
  echo "  $SELF_DIR/../toolchains"
  echo "  $SELF_DIR/toolchains"
  echo "  $HOME/toolchains"
  echo "  /root/poco/toolchains"
  echo "  /root/toolchains"
  echo "Set TC_DIR to override."
  exit 1
fi

# LLVM=1 LLVM_IAS=1 drives the build through clang + llvm binutils, so this
# toolchain must supply both; CROSS_COMPILE only supplies the --target triple.
for tool in clang ld.lld llvm-ar llvm-objcopy; do
  if [ ! -x "$TC_DIR/bin/$tool" ]; then
    echo "Toolchain $TC_DIR is missing bin/$tool"
    exit 1
  fi
done

echo -e "Using toolchain: $TC_DIR\n"
export PATH="$TC_DIR/bin:$PATH"

# Process options
CLEAN_BUILD=false
PIPELINE=false
for arg in "$@"; do
  case $arg in
  -c) CLEAN_BUILD=true ;;
  -p|--pipeline) PIPELINE=true ;;
  esac
done

[ "$CLEAN_BUILD" = true ] && rm -rf out

# Compilation process
mkdir -p out
# Clear only this script's own output; avoid a broad *zip glob that would
# match agate_and_chopin_rom_artifact.zip in this tree.
rm -rf AnyKernel3
rm -f "${KERNEL_NAME}-Chopin-"*.zip

if ! make O=out ARCH=arm64 CC="ccache clang" LLVM=1 LLVM_IAS=1 CROSS_COMPILE=aarch64-linux-gnu- CROSS_COMPILE_ARM32=arm-linux-gnueabi- KCFLAGS="-Wno-strict-prototypes -Wno-implicit-int" $DEFCONFIG > /dev/null; then
  echo -e "\nDefconfig failed!"
  exit 1
fi

echo -e "\nStarting compilation...\n"
DTB_NAME=$(grep 'CONFIG_BUILD_ARM64_APPENDED_DTB_IMAGE_NAMES=' "arch/arm64/configs/${DEFCONFIG}" 2>/dev/null | cut -d'"' -f2)
DTB_NAME="${DTB_NAME:-mediatek/mt6893}"
DTB_OUT="out/arch/arm64/boot/dts/${DTB_NAME}.dtb"

if make -j$(nproc --all) O=out ARCH=arm64 CC="ccache clang" LLVM=1 LLVM_IAS=1 CROSS_COMPILE=aarch64-linux-gnu- CROSS_COMPILE_ARM32=arm-linux-gnueabi- KCFLAGS="-Wno-strict-prototypes -Wno-implicit-int" Image.gz dtbs > /dev/null; then
  if [ ! -f "$DTB_OUT" ]; then
    echo -e "\nExpected DTB $DTB_OUT not produced!"
    exit 1
  fi
  echo -e "\nKernel + DTB compiled successfully! Zipping up...\n"
  git clone -q --depth=1 https://github.com/froyoandroid/AnyKernel3 AnyKernel3
  cp out/arch/arm64/boot/Image.gz AnyKernel3
  # Ship the base DTB so it replaces the stock boot-image dtb section on flash.
  # AnyKernel3 (magiskboot backend) repacks any file named "dtb" into the boot image.
  cp "$DTB_OUT" AnyKernel3/dtb
  (cd AnyKernel3 && zip -r9 "../$ZIPNAME" * -x '*.git*' README.md *placeholder)
  rm -rf AnyKernel3
  echo -e "\nCompleted in $((SECONDS / 60)) minute(s) and $((SECONDS % 60)) second(s)!"
  echo "Zip: $ZIPNAME"
  if [ "$PIPELINE" = true ]; then
    echo -e "\nSending zip to Telegram group..."
    COMMIT=$(git log -1 --format='%h %s' 2>/dev/null)
    python3 /root/poco/pipeline/send_zip.py "$ZIPNAME" "$KERNEL_NAME" "$SECONDS" "$COMMIT" || echo "Telegram send failed"
  fi
else
  echo -e "\nCompilation failed!"
  exit 1
fi
