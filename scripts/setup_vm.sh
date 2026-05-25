#!/bin/bash
# setup_vm.sh — Настройка виртуальной машины для разработки kprof
#
# Использование:
#   chmod +x scripts/setup_vm.sh
#   ./scripts/setup_vm.sh
#
# Требования: Ubuntu/Debian VM или хост

set -euo pipefail

echo "=== kprof: Setting up development environment ==="

# Установка зависимостей
echo "[1/4] Installing build dependencies..."
sudo apt-get update
sudo apt-get install -y \
    build-essential \
    linux-headers-$(uname -r) \
    gcc \
    make \
    git

# Проверка kprobes
echo "[2/4] Checking kernel config..."
if grep -q "CONFIG_KPROBES=y" /boot/config-$(uname -r) 2>/dev/null; then
    echo "  ✓ CONFIG_KPROBES=y"
else
    echo "  ✗ CONFIG_KPROBES not found — kprof-trace may not work"
fi

if grep -q "CONFIG_PROC_FS=y" /boot/config-$(uname -r) 2>/dev/null; then
    echo "  ✓ CONFIG_PROC_FS=y"
else
    echo "  ✗ CONFIG_PROC_FS not found"
fi

# Создание директории для сборки
echo "[3/4] Building project..."
cd "$(dirname "$0")/.."
make clean 2>/dev/null || true
make all

echo "[4/4] Done!"
echo ""
echo "Next steps:"
echo "  sudo insmod kprof-trace/kprof.ko    # Load kernel module"
echo "  kprof/kprof bench --all              # Run benchmarks"
