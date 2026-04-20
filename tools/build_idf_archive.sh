#!/usr/bin/env bash
set -euo pipefail

TARGET="${1:-esp32s3}"

if [[ "${TARGET}" != "esp32s3" ]]; then
  echo "error: this project currently supports only esp32s3 archive generation" >&2
  exit 2
fi

if ! command -v idf.py >/dev/null 2>&1; then
  echo "error: idf.py not found. Source ESP-IDF export first (e.g. '. ~/esp/esp-idf/export.sh')." >&2
  exit 2
fi

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_PROJ_DIR="${ROOT_DIR}/.idf-archive-build"
MAIN_DIR="${BUILD_PROJ_DIR}/main"
OUT_DIR="${ROOT_DIR}/src/${TARGET}"
OUT_LIB="${OUT_DIR}/libwakecore.a"
OUT_UNDEF="${OUT_DIR}/libwakecore.undefined.txt"

mkdir -p "${MAIN_DIR}"
mkdir -p "${OUT_DIR}"

cat > "${BUILD_PROJ_DIR}/CMakeLists.txt" <<CMAKE_EOF
cmake_minimum_required(VERSION 3.16)
set(EXTRA_COMPONENT_DIRS "${ROOT_DIR}")
include(\$ENV{IDF_PATH}/tools/cmake/project.cmake)
project(wakecore_archive_builder)
CMAKE_EOF

cat > "${MAIN_DIR}/CMakeLists.txt" <<'CMAKE_EOF'
idf_component_register(SRCS "dummy_main.c")
CMAKE_EOF

cat > "${MAIN_DIR}/dummy_main.c" <<'C_EOF'
void app_main(void) {}
C_EOF

# Ensure TinyUSB HID class is compiled in for Mouse descriptor enumeration.
cat > "${BUILD_PROJ_DIR}/sdkconfig.defaults" <<'KCONFIG_EOF'
CONFIG_TINYUSB_HID_COUNT=1
KCONFIG_EOF

idf.py -C "${BUILD_PROJ_DIR}" set-target "${TARGET}" >/dev/null
idf.py -C "${BUILD_PROJ_DIR}" build

NM_BIN="xtensa-${TARGET}-elf-nm"
if ! command -v "${NM_BIN}" >/dev/null 2>&1; then
  NM_BIN="nm"
fi

WAKE_ARCHIVE=""
while IFS= read -r archive; do
  if "${NM_BIN}" -g --defined-only "${archive}" 2>/dev/null | grep -Eq '\bwake_init$'; then
    WAKE_ARCHIVE="${archive}"
    break
  fi
done < <(find "${BUILD_PROJ_DIR}/build/esp-idf" -type f -name 'lib*.a' | sort)

if [[ -z "${WAKE_ARCHIVE}" ]]; then
  echo "error: failed to locate a component archive containing wake_init" >&2
  exit 3
fi

cp "${WAKE_ARCHIVE}" "${OUT_LIB}"
"${NM_BIN}" -u "${OUT_LIB}" | sort -u > "${OUT_UNDEF}" || true

echo "generated: ${OUT_LIB}"
echo "symbols:   ${OUT_UNDEF}"
