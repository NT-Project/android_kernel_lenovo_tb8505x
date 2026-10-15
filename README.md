# Pure-Breakdown Kernel for Lenovo Tab M8 TB-8505X

Custom Linux 4.9 kernel for the **Lenovo Tab M8 TB-8505X (LTE)**, based on Lenovo OSS sources and upstreamed to **Linux 4.9.200**.

**Telegram:** https://t.me/len8505x  
**Repository:** https://github.com/kin203/android_kernel_lenovo_tb8505x

## Branches

| Branch | Description | CPU | GPU | KernelSU |
|---|---|---:|---:|---|
| `pure-breakdown-v1` | Stable / stock clock | 2.001 GHz | 660 MHz | No |
| `pure-breakdown-v1-oc` | Performance / OC | 2.201 GHz | 730 MHz | No |
| `pure-breakdown-v1-ksu-umount-oc` | OC + KernelSU + module unmount | 2.201 GHz | 730 MHz | KernelSU v0.9.5 |
| `upstream-4.9.y` | Upstream integration base | Stock | Stock | No |

## Releases

- **v1.0** — Linux 4.9.200, stock clocks
- **v1.0-oc** — CPU 2.201 GHz + GPU 730 MHz
- **v1.0-ksu-umount-oc** — KernelSU v0.9.5 + manual hooks + `path_umount` + OC

## KernelSU Build

The KernelSU branch uses:

- KernelSU **v0.9.5**
- Legacy/manual hooks
- `CONFIG_KSU=y`
- `CONFIG_KPROBES=n`
- `path_umount()` backport for KernelSU module-unmount support
- SELinux **Enforcing**

KernelSU is tracked as a Git submodule.

Clone with:

```bash
git clone --recursive https://github.com/kin203/android_kernel_lenovo_tb8505x.git
```

If already cloned:

```bash
git submodule update --init --recursive
```

## Toolchain

Known-good compiler setup:

- Android Clang **r353983c / Clang 9.0.3**
- AArch64 GCC 4.9 toolchain support
- Python 2 required by the vendor build system

## Build Instructions

Checkout the desired branch:

```bash
git switch pure-breakdown-v1

# Or:
# git switch pure-breakdown-v1-oc
# git switch pure-breakdown-v1-ksu-umount-oc
```

Set the build environment:

```bash
export ARCH=arm64
export SUBARCH=arm64
export CROSS_COMPILE="$PWD/prebuilds/toolchain/aarch64-linux-android-4.9/bin/aarch64-linux-android-"
export CLANG_TRIPLE=aarch64-linux-gnu-
export CC="$PWD/prebuilds/toolchain/clang-r353983c/bin/clang"
```

Generate the LTE configuration:

```bash
make O=out ARCH=arm64 akita_row_call_defconfig
```

Build:

```bash
make -j4 \
  O=out \
  ARCH=arm64 \
  SUBARCH=arm64 \
  CROSS_COMPILE="$CROSS_COMPILE" \
  CLANG_TRIPLE="$CLANG_TRIPLE" \
  CC="$CC" \
  python=python2 \
  EXTRA_CFLAGS="-Wno-error -Wno-error=strict-prototypes"
```

Kernel output:

```text
out/arch/arm64/boot/Image.gz-dtb
```

## AnyKernel3 Packaging

Copy the kernel image into your TB-8505X AnyKernel3 tree:

```bash
cp out/arch/arm64/boot/Image.gz-dtb /path/to/anykernel-tb8505x/
cd /path/to/anykernel-tb8505x
```

Package:

```bash
zip -r9 Pure-Breakdown-TB8505X.zip . \
  -x "*.git*" \
  -x "*.zip"
```

## Flashing

- Tested with TWRP
- No wipe required
- Keep a known-good stock boot backup before flashing
- Do **not** format `/data`
- Do **not** flash unverified boot images

## Overclock Profile

OC branches use:

- CPU: **2.201 GHz**
- GPU: **730 MHz**
- CPU top voltage: **1.025 V**
- GPU voltage: **0.800 V**
- GPU VSRAM: **0.875 V**
- Stock thermal, PBM and battery protections retained

The OC profile was tested on the target TB-8505X device. Silicon quality varies, so use the stock-clock branch if stability is the priority.

## Device

- Device: Lenovo Tab M8
- Model: TB-8505X
- SoC: MediaTek MT6761 / Helio A22
- Architecture: arm64
- Android base tested: Android 10
- Kernel base: Lenovo OSS 4.9.190, upstreamed to 4.9.200

## Credits

- Lenovo / MediaTek for kernel sources
- Linux stable maintainers
- KernelSU project
- Android kernel community

## Disclaimer

Use at your own risk. Always keep a stock boot image and working recovery available before flashing custom kernels.
