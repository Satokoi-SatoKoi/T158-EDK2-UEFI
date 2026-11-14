#!/usr/bin/env bash
# ------------------------------------------------------------------------------
#  T158 (UMS9621) UEFI —— 一键构建
#
#  目录约定:
#    ~/edk2                      EDK2 主仓 (建议 edk2-stable202405)
#    <本工程>/edk2/T158Pkg       平台包
#
#  用法:  ./build.sh [DEBUG|RELEASE]
# ------------------------------------------------------------------------------
set -e

TARGET=${1:-DEBUG}
HERE="$(cd "$(dirname "$0")" && pwd)"
EDK2=${EDK2:-"$HERE/../edk2"}

if [ ! -d "$EDK2" ]; then
  echo "未找到 EDK2: $EDK2"
  echo "请: git clone https://github.com/tianocore/edk2.git && git checkout edk2-stable202405"
  echo "    或设置环境变量 EDK2=<edk2 路径>"
  exit 1
fi

# 环境变量 / 工具链探测 / BaseTools / edksetup 全部在 envsetup.sh 里
# shellcheck source=envsetup.sh
. "$HERE/envsetup.sh" || exit 1

build -a AARCH64 -t GCC5 -p T158Pkg/T158Pkg.dsc -b "$TARGET"

FD="$EDK2/Build/T158/${TARGET}_GCC5/FV/T158.fd"
echo "FD: $FD"

# 可选第 2 参数: 原厂安卓 boot.img 备份 -> 内嵌其 kernel, 生成自包含 payload
# (只刷 boot_a 即可启动安卓, 不依赖 boot_b)。用法: ./build.sh DEBUG boot_a_backup.img
EMBED=()
if [ -n "$2" ]; then
  if [ ! -f "$2" ]; then
    echo "内嵌镜像不存在: $2"
    exit 1
  fi
  EMBED=(-k "$2")
  echo "自包含模式: 内嵌安卓 kernel 来自 $2"
fi

"${PYTHON_COMMAND:-python3}" "$HERE/tools/mkpayload.py" bootimg -i "$FD" "${EMBED[@]}" -o "$HERE/payload.img"
"${PYTHON_COMMAND:-python3}" "$HERE/tools/mkpayload.py" raw    -i "$FD" -o "$HERE/uefi_payload.bin"

echo
echo "产物:"
echo "  $HERE/payload.img        可直接 fastboot boot / flash boot_a 的 boot.img (v4)"
echo "  $HERE/uefi_payload.bin   裸载荷 (供 ResearchDownload 等工具封装)"
echo
echo "下一步: fastboot boot payload.img   (需先解除 AVB, 见 docs/03)"
