#!/usr/bin/env bash
# ------------------------------------------------------------------------------
#  T158 (UMS9621) UEFI —— EDK2 构建环境配置
#
#  适用: Linux / Ubuntu 虚拟机 / Windows MSYS2 (MSYS 终端)
#
#  用法 (必须是 source, 不能直接执行):
#       . envsetup.sh
#       build -a AARCH64 -t GCC5 -p T158Pkg/T158Pkg.dsc -b DEBUG
#
#  或直接用 build.sh (它会自动 source 本文件):
#       ./build.sh DEBUG
#
#  可预设的环境变量 (不设则自动探测):
#       EDK2                    edk2 主仓路径,     默认 <本工程>/../edk2
#       GCC5_AARCH64_PREFIX     aarch64 交叉前缀,  默认自动探测
#       PYTHON_COMMAND          python3 路径,      默认 python3
# ------------------------------------------------------------------------------

# 防止被直接执行 (source 时环境变量才会生效)
if [ "${BASH_SOURCE[0]}" = "$0" ]; then
  echo "错误: 请用 source 方式执行:  . envsetup.sh"
  exit 1
fi

# ------------------------------------------------------------------------ 路径
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
EDK2="${EDK2:-$HERE/../edk2}"

if [ ! -d "$EDK2/BaseTools" ]; then
  echo "未找到 EDK2: $EDK2"
  echo "  git clone --depth 1 -b edk2-stable202405 --recurse-submodules \\"
  echo "            --shallow-submodules https://github.com/tianocore/edk2.git"
  echo "  <edk2 路径>   或: export EDK2=/path/to/edk2"
  return 1
fi
EDK2="$(cd "$EDK2" && pwd)"

# ------------------------------------------------------------- aarch64 工具链
# 常见安装位置自动加入 PATH (主要是 Windows 上手工解压的情况)
for _d in /c/arm-gnu/*/bin \
          "/c/Program Files/arm-gnu"/*/bin \
          /opt/arm-gnu/*/bin \
          "$HOME"/arm-gnu/*/bin \
          /usr/bin; do
  if [ -d "$_d" ]; then PATH="$_d:$PATH"; fi
done
unset _d
export PATH

GCC5_AARCH64_PREFIX="${GCC5_AARCH64_PREFIX:-}"

if [ -z "$GCC5_AARCH64_PREFIX" ]; then
  for _p in aarch64-linux-gnu- aarch64-none-elf- aarch64-none-linux-gnu- aarch64-elf-; do
    if command -v "${_p}gcc" >/dev/null 2>&1; then
      GCC5_AARCH64_PREFIX="$_p"
      break
    fi
  done
  unset _p
fi

if [ -z "$GCC5_AARCH64_PREFIX" ]; then
  echo "未找到 aarch64 交叉编译器 (aarch64-linux-gnu-gcc / aarch64-none-elf-gcc)"
  echo "  Ubuntu : sudo apt install -y gcc-aarch64-linux-gnu"
  echo "  Windows: https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads"
  echo "           (AArch64 bare-metal, mingw-w64-i686 host)"
  return 1
fi
export GCC5_AARCH64_PREFIX

# -------------------------------------------------------------------- Python
export PYTHON_COMMAND="${PYTHON_COMMAND:-python3}"
if ! command -v "$PYTHON_COMMAND" >/dev/null 2>&1; then
  echo "未找到 python3, 请安装后重试 (或 export PYTHON_COMMAND=/path/to/python3)"
  return 1
fi

# --------------------------------------------------------------- EDK2 变量
export WORKSPACE="$EDK2"
export EDK_TOOLS_PATH="$EDK2/BaseTools"
export PACKAGES_PATH="$EDK2:$HERE/edk2"
export CONF_PATH="$EDK2/Conf"

# ------------------------------------------------------------- BaseTools
# 只校验真正会用到的 host 工具; BrotliCompress 缺 brotli 子模块时允许失败,
# 本工程 (T158Pkg) 不使用 Brotli 压缩, 不影响构建。
TOOLS_BIN="$EDK_TOOLS_PATH/Source/C/bin"
# 注意: 不含 BrotliCompress (缺 brotli 子模块, 本工程不用 Brotli 压缩)
#       不含 VfrCompile   (需要 g++; 只有编 .vfr 表单才用, 本工程无 VFR)
NEED_TOOLS="GenFfs GenFv GenFw GenSec LzmaCompress TianoCompress GenCrc32 DevicePath EfiRom VolInfo"
tools_ok () {
  local t
  for t in $NEED_TOOLS; do
    if [ ! -x "$TOOLS_BIN/$t" ]; then return 1; fi
  done
  return 0
}

if ! tools_ok; then
  echo "[1/2] 编译 BaseTools (make -k, 允许 BrotliCompress 失败) ..."
  make -C "$EDK_TOOLS_PATH/Source/C" -k >>/tmp/edk2_basetools.log 2>&1 || true
fi

if ! tools_ok; then
  echo "BaseTools 编译失败, 缺关键工具。日志尾部:"
  tail -25 /tmp/edk2_basetools.log 2>/dev/null || true
  echo "完整日志: /tmp/edk2_basetools.log"
  return 1
fi

# ------------------------------------------------------------- edksetup
echo "[2/2] edksetup ..."
cd "$EDK2" || return 1
# shellcheck disable=SC1091
. edksetup.sh BaseTools

# ------------------------------------------------------------------ 摘要
echo
echo "==================== EDK2 环境已就绪 ===================="
echo "  WORKSPACE        : $WORKSPACE"
echo "  PACKAGES_PATH    : $PACKAGES_PATH"
echo "  TOOLCHAIN        : GCC5  ($GCC5_AARCH64_PREFIX)"
echo "  CC               : $("${GCC5_AARCH64_PREFIX}gcc" -dumpversion)"
echo "  PYTHON           : $PYTHON_COMMAND ($("$PYTHON_COMMAND" --version 2>&1))"
echo "  TARGET           : AARCH64"
echo "---------------------------------------------------------"
echo "  T158Pkg 查找     : $([ -d "$HERE/edk2/T158Pkg" ] && echo "$HERE/edk2/T158Pkg (OK)" || echo '未找到!')"
echo
echo "  下一步: build -a AARCH64 -t GCC5 -p T158Pkg/T158Pkg.dsc -b DEBUG"
echo "          或直接: $HERE/build.sh DEBUG"
echo "========================================================="
