#!/bin/bash
# ============================================================
# SF32LB52 温控面板 - OpenVela (NuttX) Ubuntu 22.04 编译环境一键准备
# 运行:  bash setup_openvela_env.sh          (阶段 A/B)
# 可选:  bash setup_openvela_env.sh --with-source  (含源码下载)
# ============================================================
set -e

echo "============================================"
echo "  OpenVela Build Environment (Ubuntu 22.04)"
echo "============================================"

# ============ 阶段 A: 系统依赖包 ============
echo "[A/1] 安装系统依赖..."
sudo apt update
sudo apt install -y \
    git cmake python3 python3-pip build-essential python-is-python3 \
    bison flex \
    libgmp-dev libmpc-dev libmpfr-dev libisl-dev binutils-dev libelf-dev \
    libexpat1-dev gcc-multilib g++-multilib \
    gcc-arm-none-eabi binutils-arm-none-eabi \
    picocom u-boot-tools util-linux dfu-util pkgconf net-tools mtools \
    libusb-1.0-0-dev libx11-dev libxext-dev zlib1g-dev

# ============ 阶段 B: 工具链 ============
echo "[B/1] 安装 repo 工具..."
if [ ! -x /usr/local/bin/repo ]; then
    curl -sSL "https://mirrors.tuna.tsinghua.edu.cn/git/git-repo" -o /tmp/repo
    chmod +x /tmp/repo
    sudo mv /tmp/repo /usr/local/bin/repo
fi

echo "[B/2] 安装 KConfig 前端..."
sudo apt install -y kconfig-frontends 2>/dev/null || echo "  (kconfig-frontends 缺失, 后续可从 NuttX tools 源码构建)"

echo "[B/3] 安装 Python 依赖..."
sudo pip3 install kconfiglib pyelftools cxxfilt 2>/dev/null || \
    sudo pip3 install --break-system-packages kconfiglib pyelftools cxxfilt

echo "[B/4] 初始化 Git LFS (大文件必须对齐)..."
sudo apt install -y git-lfs 2>/dev/null || true
git lfs install 2>/dev/null || sudo git lfs install

# ============ 验证 ============
echo ""
echo "============================================"
echo "  验证工具链"
echo "============================================"
arm-none-eabi-gcc --version | head -1
arm-none-eabi-gcc  -print-multiarch 2>/dev/null || true
repo --version 2>/dev/null | head -1 || echo "  repo: 未安装(可忽略, 除非需要 repo 拉源码)"
python3 --version
kconfig-conf --version 2>/dev/null || echo "  kconfig: 使用 kconfiglib 代替"
echo "  OK: 环境准备完成"

# ============ 阶段 C (可选): 源码下载 ============
# 源码仓库 >5GB, 需要 LFS, 仅当显式传入 --with-source 时执行
if [ "$1" = "--with-source" ]; then
    echo ""
    echo "============================================"
    echo "[C] 下载 OpenVela 源码 (请按需修改分支/平台)"
    echo "============================================"
    SRC_DIR="${OPENVELA_SRC_DIR:-$HOME/openvela}"
    mkdir -p "$SRC_DIR" && cd "$SRC_DIR"
    if [ ! -d .repo ]; then
        # 从 Gitee 拉取, 如需 GitHub 请改 URL
        repo init \
            -u https://gitee.com/open-vela/manifests.git \
            -b trunk \
            -m openvela.xml \
            --repo-url=https://mirrors.tuna.tsinghua.edu.cn/git/git-repo/ \
            --git-lfs
    fi
    repo sync -c -j8
    echo "  源码已同步到: $SRC_DIR"
else
    echo ""
    echo "============================================"
    echo "  提示: 如需一并下载 OpenVela 源码 (~5GB),"
    echo "  请运行:  bash $0 --with-source"
    echo "============================================"
fi