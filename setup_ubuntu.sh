#!/usr/bin/env bash
# ------------------------------------------------------------------------------
#  T158 (UMS9621) UEFI —— Ubuntu / Debian 构建环境配置 (不安装任何系统依赖)
#
#  前提: 以下工具需已存在 (脚本只检查, 不安装)
#        gcc  aarch64-linux-gnu-gcc  make  python3  tar
#
#  EDK2 获取:
#    * EDK2 已存在      -> 直接用
#    * --archive <文件> -> 本地压缩包 (tar.gz / zip)
#    * 自动扫描          -> ../edk2*.tar.gz  ~/edk2*.tar.gz
#    * 多镜像下载        -> GitHub 直连 / ghproxy / gh-proxy / gh.llkk.cc
#    * --git            -> git clone
#
#  用法:
#       bash setup_ubuntu.sh --no-git --build        # EDK2 已就位, 直接构建
#       bash setup_ubuntu.sh --archive ~/edk2-stable202405.tar.gz
#       bash setup_ubuntu.sh --git --proxy           # git clone 并走 ghproxy
#       bash setup_ubuntu.sh --build -b RELEASE
#
#  首次请用 `bash setup_ubuntu.sh` 启动 (文件若是 CRLF, `./` 会因 ^M 失败),
#  脚本第 2 步会修好换行符, 之后就能 `./setup_ubuntu.sh` 了。
# ------------------------------------------------------------------------------
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
EDK2_ARG="${EDK2:-$HERE/../edk2}"
EDK2_ARCHIVE="${EDK2_ARCHIVE:-}"
DO_BUILD=0
DO_PROXY=0
USE_GIT=0
NO_GIT=0
TARGET="DEBUG"
EDK2_TAG="edk2-stable202405"
TARBALL="https://github.com/tianocore/edk2/archive/refs/tags/${EDK2_TAG}.tar.gz"
BROTLI_TARBALL="https://github.com/google/brotli/archive/refs/heads/master.tar.gz"
MIRRORS=("" "https://ghproxy.net/" "https://gh-proxy.com/" "https://gh.llkk.cc/" "https://mirror.ghproxy.com/")

while [ $# -gt 0 ]; do
  case "$1" in
    --build)     DO_BUILD=1 ;;
    --git)       USE_GIT=1 ;;
    --proxy)     DO_PROXY=1 ;;
    --no-git)    NO_GIT=1 ;;
    --archive)   shift; EDK2_ARCHIVE="${1:-}" ;;
    --no-clone)  USE_GIT=0 ;;
    -b|--build-target) shift; TARGET="${1:-DEBUG}" ;;
    -h|--help)   sed -n '2,28p' "$0"; exit 0 ;;
    *) echo "未知参数: $1 (用 --help 查看用法)"; exit 1 ;;
  esac
  shift
done

say () { printf '\n\033[1;36m==> %s\033[0m\n' "$*"; }

echo "工程目录 : $HERE"
echo "EDK2 目录: $EDK2_ARG"

# 共享文件夹 / Windows 挂载点没有可执行位与符号链接, EDK2 在这些位置必定编译失败
case "$HERE" in
  /mnt/hgfs/*|/media/sf_*|/media/*/sf_*|/mnt/[cdeCDE]/*|/cygdrive/*)
    echo
    echo "错误: 工程位于共享文件夹或 Windows 挂载路径: $HERE"
    echo "      EDK2 需要可执行位和符号链接, 在那里无法编译。"
    echo "      请先复制到 Ubuntu 本地磁盘 (ext4):"
    echo "        cp -r '$HERE' ~/t158-uefi"
    echo "        cd ~/t158-uefi && bash setup_ubuntu.sh --no-git --build"
    exit 1
    ;;
esac

# ------------------------------------------------------- 1. 依赖检查 (不安装)
say "[1/5] 检查依赖 (不安装)"
MISSING=""
for c in gcc aarch64-linux-gnu-gcc make python3 tar; do
  if ! command -v "$c" >/dev/null 2>&1; then MISSING="$MISSING $c"; fi
done
if [ -n "$MISSING" ]; then
  echo "缺少:$MISSING"
  echo "  请自行安装, 例如:"
  echo "  sudo apt-get install -y build-essential gcc-aarch64-linux-gnu uuid-dev python3"
else
  echo "依赖齐全 (gcc / aarch64-linux-gnu-gcc / make / python3 / tar)"
fi

# ------------------------------------------------------- 2. 修 CRLF (含自己)
say "[2/5] 修正换行符 (CRLF -> LF)"
find "$HERE" -type f \( -name '*.sh' -o -name '*.py' -o -name '*.c' -o -name '*.h' \
     -o -name '*.inf' -o -name '*.dsc' -o -name '*.dec' -o -name '*.fdf' -o -name '*.txt' \
  \) -exec sed -i 's/\r$//' {} + 2>/dev/null || true
find "$HERE" -name '__pycache__' -type d -exec rm -rf {} + 2>/dev/null || true
chmod +x "$HERE"/*.sh 2>/dev/null || true

# --------------------------------------------------------- 3. 下载辅助函数
dl () {
  local out="$1" base="$2" m url
  for m in "${MIRRORS[@]}"; do
    url="${m}${base}"
    printf '    尝试 %s ... ' "$url"
    if curl -fL --connect-timeout 12 --max-time 900 --retry 2 \
            -o "$out" "$url" >/dev/null 2>&1 && [ -s "$out" ]; then
      echo "OK"; return 0
    fi
    echo "失败"
  done
  return 1
}

_parent="$(dirname "$EDK2_ARG")"
mkdir -p "$_parent" 2>/dev/null || true
EDK2="$(cd "$_parent" && pwd)/$(basename "$EDK2_ARG")"
unset _parent

# ------------------------------------------------------------ 4. 准备 EDK2
say "[3/5] 准备 EDK2 ($EDK2_TAG)"

# 自动归位: 若 EDK2 被解压/克隆到了工程目录内 (如 t158-uefi/edk2-edk2-stable202405),
# 挪到工程同级的 ~/edk2。只处理"含 BaseTools 且不含 T158Pkg"的目录, 不会误伤平台包。
if [ ! -d "$EDK2/BaseTools" ]; then
  _cand="$(find "$HERE" -maxdepth 2 -type d -name BaseTools -print -quit 2>/dev/null || true)"
  if [ -n "$_cand" ]; then
    _cand="$(dirname "$_cand")"
    if [ "$_cand" != "$HERE/edk2" ] && [ ! -d "$_cand/T158Pkg" ]; then
      echo "发现工程内的 EDK2: $_cand"
      echo "自动移动到 $EDK2 ..."
      mkdir -p "$(dirname "$EDK2")"
      mv "$_cand" "$EDK2"
    fi
  fi
  unset _cand
fi

if [ "$NO_GIT" -eq 1 ]; then
  if [ -d "$EDK2/BaseTools" ]; then
    echo "EDK2 已存在: $EDK2  (--no-git, 跳过获取)"
  else
    echo "--no-git 已指定, 但未找到 EDK2: $EDK2"
    echo "  请设置 EDK2=<路径> 或去掉 --no-git"
    exit 1
  fi
elif [ -d "$EDK2/BaseTools" ]; then
  echo "已存在: $EDK2 (跳过)"
else
  # --- 4a. 找本地压缩包
  if [ -z "$EDK2_ARCHIVE" ]; then
    for f in "$EDK2_ARG"*.tar.gz "$EDK2_ARG"*.zip \
             "$HERE"/../edk2*.tar.gz "$HERE"/../edk2*.zip \
             "$HERE"/edk2*.tar.gz "$HERE"/edk2*.zip \
             "$HOME"/edk2*.tar.gz "$HOME"/edk2*.zip; do
      if [ -f "$f" ]; then EDK2_ARCHIVE="$f"; break; fi
    done
  fi

  if [ -n "$EDK2_ARCHIVE" ] && [ -f "$EDK2_ARCHIVE" ]; then
    echo "使用本地压缩包: $EDK2_ARCHIVE"
  elif [ "$USE_GIT" -eq 0 ]; then
    echo "未找到本地包, 尝试多镜像下载 (约 200MB) ..."
    if dl "$HOME/${EDK2_TAG}.tar.gz" "$TARBALL"; then
      EDK2_ARCHIVE="$HOME/${EDK2_TAG}.tar.gz"
    fi
  fi

  if [ -n "$EDK2_ARCHIVE" ] && [ -f "$EDK2_ARCHIVE" ]; then
    tmp="$(mktemp -d)"
    case "$EDK2_ARCHIVE" in
      *.zip) unzip -q "$EDK2_ARCHIVE" -d "$tmp" ;;
      *)     tar xzf "$EDK2_ARCHIVE" -C "$tmp" ;;
    esac
    src="$(find "$tmp" -maxdepth 2 -type d -name BaseTools -print -quit 2>/dev/null || true)"
    if [ -z "$src" ]; then
      echo "压缩包结构异常: 未找到 BaseTools 目录"; rm -rf "$tmp"; exit 1
    fi
    src="$(dirname "$src")"
    mkdir -p "$EDK2"
    cp -a "$src"/. "$EDK2"/
    rm -rf "$tmp"
    echo "已解压到 $EDK2"

    # --- 4b. 补 brotli 子模块: BaseTools 编译 BrotliCompress 需要它
    if [ ! -d "$EDK2/BaseTools/Source/C/BrotliCompress/brotli/c" ]; then
      echo "补齐 brotli 子模块 (BaseTools/BrotliCompress 需要) ..."
      btmp="$(mktemp -d)"
      BPKG=""
      for f in "$HOME"/brotli*.tar.gz "$HERE"/../brotli*.tar.gz "$HERE"/brotli*.tar.gz; do
        if [ -f "$f" ]; then BPKG="$f"; break; fi
      done
      got=0
      if [ -n "$BPKG" ] && [ -f "$BPKG" ]; then
        echo "使用本地 brotli 包: $BPKG"
        cp "$BPKG" "$btmp/brotli.tar.gz"
        got=1
      elif dl "$btmp/brotli.tar.gz" "$BROTLI_TARBALL"; then
        got=1
      fi
      if [ "$got" -eq 1 ]; then
        tar xzf "$btmp/brotli.tar.gz" -C "$btmp"
        bsrc="$(find "$btmp" -maxdepth 2 -type d -name c -print -quit 2>/dev/null || true)"
        if [ -n "$bsrc" ]; then
          mkdir -p "$EDK2/BaseTools/Source/C/BrotliCompress/brotli"
          cp -a "$(dirname "$bsrc")"/. "$EDK2/BaseTools/Source/C/BrotliCompress/brotli"/
          echo "brotli 已就位"
        fi
      else
        echo "警告: brotli 获取失败 -> 将跳过 BrotliCompress (不影响本工程构建)"
      fi
      rm -rf "$btmp"
    fi
  else
    echo "改用 git clone ..."
    if [ "$DO_PROXY" -eq 1 ]; then
      git config --global url."https://ghproxy.net/https://github.com/".insteadOf "https://github.com/"
      echo "已配置 git 走 ghproxy"
    fi
    git clone --depth 1 -b "$EDK2_TAG" --recurse-submodules --shallow-submodules \
        https://github.com/tianocore/edk2.git "$EDK2"
  fi
fi
export EDK2

# --------------------------------------------------- 5. BaseTools + 环境变量
say "[4/5] 编译 BaseTools 并配置 EDK2 环境"
cd "$HERE"
# shellcheck source=envsetup.sh
. "$HERE/envsetup.sh"

# ---------------------------------------------------------------- 6. 完成
say "[5/5] 完成"
echo "EDK2 版本 : $(git -C "$EDK2" describe --tags 2>/dev/null || echo "$EDK2_TAG (archive)")"
echo "编译器    : $("${GCC5_AARCH64_PREFIX}gcc" -dumpversion)"
echo "产物目录  : $EDK2/Build/T158/"

if [ "$DO_BUILD" -eq 1 ]; then
  say "开始构建 ($TARGET)"
  "$HERE/build.sh" "$TARGET"
  echo
  echo "产物:"
  echo "  $HERE/payload.img        <- fastboot flash boot_a"
  echo "  $HERE/uefi_payload.bin"
else
  echo "下一步:  cd $HERE && ./build.sh DEBUG"
  echo "产物:    $HERE/payload.img"
fi
