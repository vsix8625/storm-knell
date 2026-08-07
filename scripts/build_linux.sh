#!/usr/bin/env bash
set -euo pipefail

# Do not run as root
if [ "${EUID}" -eq 0 ]; then
    echo "[build.sh]: Error: Do not run the bootstrap as root." >&2
    exit 1
fi

START_TIME=$(date +%s%N)
BUILD_DIR="bootstrap_stage"

echo "[build.sh]: Creating build staging directory: '${BUILD_DIR}'..."
mkdir -p "${BUILD_DIR}"

# --- Compiler & Linker Selection ---
echo "[build.sh]: Searching for a suitable C compiler..."
CC=""
LDFLAGS=()

if command -v clang >/dev/null 2>&1; then
    CC="clang"
    echo "[build.sh]: Found compiler: clang"
    if command -v mold >/dev/null 2>&1; then
        echo "[build.sh]: Found high-performance linker 'mold'. Enabling '-fuse-ld=mold'."
        LDFLAGS+=("-fuse-ld=mold")
    fi
elif command -v gcc >/dev/null 2>&1; then
    CC="gcc"
    echo "[build.sh]: Found compiler: gcc"
elif command -v cc >/dev/null 2>&1; then
    CC="cc"
    echo "[build.sh]: Found compiler: cc"
else
    echo "[build.sh]: Error: No compatible C compiler found (checked clang, gcc, cc)." >&2
    exit 1
fi

C23_FLAG="-std=c23"
if [[ "${CC}" == *"gcc"* ]] || "${CC}" --version 2>&1 | grep -iq "gcc"; then
    GCC_MAJOR=$("${CC}" -dumpversion 2>/dev/null | cut -d. -f1 || echo "0")
    if [ "${GCC_MAJOR}" -ne 0 ] && [ "${GCC_MAJOR}" -lt 14 ]; then
        C23_FLAG="-std=c2x"
    fi
    echo "[build.sh]: Detected GCC version ${GCC_MAJOR}. Using flag: ${C23_FLAG}"
fi

# --- Include Paths & Sources ---
INCLUDE_PATHS=(
    "src"
    "src/core"
    "src/core/cli"
    "src/core/commands"
    "src/core/dsl"
    "src/core/hash"
    "src/core/invoke"
    "src/core/memory"
    "src/core/sk_cache"
    "src/core/util"
    "external/vx/include"
)

echo "[build.sh]: Discovering C source files..."
SOURCES=()
for root_dir in "src" "external/vx/src"; do
    if [ -d "${root_dir}" ]; then
        while IFS= read -r -d '' file; do
            SOURCES+=("${file}")
        done < <(find "${root_dir}" -type f -name "*.c" -print0)
    else
        echo "[build.sh]: Note: Directory '${root_dir}' does not exist, skipping."
    fi
done

echo "[build.sh]: Collected ${#SOURCES[@]} source file(s) for compilation."

if [ "${#SOURCES[@]}" -eq 0 ]; then
    echo "[build.sh]: Error: No C source files found." >&2
    exit 1
fi

OUT_EXE="${BUILD_DIR}/sk_bootstrap"

INCLUDES=()
for path in "${INCLUDE_PATHS[@]}"; do
    INCLUDES+=("-I${path}")
done

ARGS=(
    "${CC}"
    "${C23_FLAG}"
    "-O1"
    "-Wall"
    "-Wextra"
    "-Werror"
    "-D_GNU_SOURCE"
    "${INCLUDES[@]}"
    "${LDFLAGS[@]}"
    "${SOURCES[@]}"
    "-lxxhash"
    "-lpthread"
    "-o"
    "${OUT_EXE}"
)

echo ""
echo "[build.sh]: Executing staging compilation command:"
printf "%q " "${ARGS[@]}"
echo ""
echo "--------------------------------------------------"

"${ARGS[@]}"

echo "--------------------------------------------------"
echo "[build.sh]: Staging compilation finished successfully."

cleanup() {
    echo ""
    echo "[build.sh]: Cleaning up staging directory: '${BUILD_DIR}'..."
    rm -rf "${BUILD_DIR}"
}
trap cleanup EXIT

if [ -f "${OUT_EXE}" ]; then
    echo ""
    echo "[build.sh]: Bootstrap binary generated at '${OUT_EXE}'."
    echo "[build.sh]: Handing over execution to Storm-Knell..."
    echo ""

    HANDOVER_CMD=("./${OUT_EXE}" "init" "strike" "--profile" "--set=bootstrap")

    echo "[build.sh]: Running handover command:"
    printf "%q " "${HANDOVER_CMD[@]}"
    echo ""
    echo "--------------------------------------------------"

    "${HANDOVER_CMD[@]}"

    echo "--------------------------------------------------"
    echo "[build.sh]: Storm-Knell native execution completed successfully."
else
    echo "[build.sh]: Error: Bootstrap binary was not generated." >&2
    exit 1
fi

END_TIME=$(date +%s%N)
ELAPSED=$(awk -v start="${START_TIME}" -v end="${END_TIME}" 'BEGIN { printf "%.2f", (end - start) / 1000000000 }')

echo "--------------------------------------------------"
echo "[build.sh]: Total bootstrap process took ${ELAPSED}s"
