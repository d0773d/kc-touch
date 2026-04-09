#!/usr/bin/env bash
set -euo pipefail

# Reproducible Linux VPS build helper for YamUI on ESP-IDF 5.5.1.
# Usage:
#   ./tools/linux_vps_build.sh
#   ./tools/linux_vps_build.sh --project-dir /path/to/YamUI
#   ./tools/linux_vps_build.sh --skip-system-deps

IDF_VERSION="v5.5.1"
IDF_DIR_DEFAULT="${HOME}/esp/esp-idf-${IDF_VERSION}"
PROJECT_DIR_DEFAULT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SKIP_SYSTEM_DEPS=0
PROJECT_DIR="${PROJECT_DIR_DEFAULT}"
IDF_DIR="${IDF_DIR_DEFAULT}"

while [[ $# -gt 0 ]]; do
  case "$1" in
    --project-dir)
      PROJECT_DIR="$2"
      shift 2
      ;;
    --idf-dir)
      IDF_DIR="$2"
      shift 2
      ;;
    --skip-system-deps)
      SKIP_SYSTEM_DEPS=1
      shift
      ;;
    -h|--help)
      cat <<'EOF'
linux_vps_build.sh

Options:
  --project-dir <path>    YamUI project root (default: repo root)
  --idf-dir <path>        ESP-IDF install dir (default: ~/esp/esp-idf-v5.5.1)
  --skip-system-deps      Skip apt package installation
  -h, --help              Show this help
EOF
      exit 0
      ;;
    *)
      echo "Unknown argument: $1" >&2
      exit 1
      ;;
  esac
done

if [[ ! -d "${PROJECT_DIR}" ]]; then
  echo "Project directory does not exist: ${PROJECT_DIR}" >&2
  exit 1
fi

if [[ ${SKIP_SYSTEM_DEPS} -eq 0 ]]; then
  if command -v apt-get >/dev/null 2>&1; then
    echo "[1/6] Installing system dependencies via apt..."
    sudo apt-get update
    sudo apt-get install -y \
      git curl wget flex bison gperf \
      python3 python3-pip python3-venv python3-setuptools python3-wheel \
      cmake ninja-build ccache libffi-dev libssl-dev dfu-util libusb-1.0-0
  else
    echo "apt-get not found. Re-run with --skip-system-deps if dependencies are already installed." >&2
    exit 1
  fi
fi

mkdir -p "$(dirname "${IDF_DIR}")"

if [[ ! -d "${IDF_DIR}" ]]; then
  echo "[2/6] Cloning ESP-IDF ${IDF_VERSION} into ${IDF_DIR}..."
  git clone --recursive --branch "${IDF_VERSION}" https://github.com/espressif/esp-idf.git "${IDF_DIR}"
else
  echo "[2/6] Reusing existing ESP-IDF directory: ${IDF_DIR}"
fi

echo "[3/6] Installing ESP-IDF tools..."
"${IDF_DIR}/install.sh" esp32p4

echo "[4/6] Exporting ESP-IDF environment..."
set +u
source "${IDF_DIR}/export.sh"
set -u

echo "[5/6] Running clean build in ${PROJECT_DIR}..."
cd "${PROJECT_DIR}"
idf.py --version
idf.py set-target esp32p4
idf.py fullclean build

echo "[6/6] Build complete."
echo "Artifacts:"
echo "  ${PROJECT_DIR}/build/YamUI.bin"
echo "  ${PROJECT_DIR}/build/bootloader/bootloader.bin"
echo "  ${PROJECT_DIR}/build/partition_table/partition-table.bin"
