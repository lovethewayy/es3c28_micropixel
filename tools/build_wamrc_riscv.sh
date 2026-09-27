#!/usr/bin/env bash
set -euo pipefail

# Build a self-contained LLVM (RISCV backend) + wamrc for riscv32 AOT.
# Mirrors the official build_llvm.py flow (build --target package, then
# repackage) so the cache only needs the build dir, not the LLVM sources.

workspace_root="$(cd "$(dirname "$0")/.." && pwd)"
wamr_root="$workspace_root/firmware/espressif/components/wasm-micro-runtime"
work_dir="${RISCV_LLVM_WORK_DIR:-$workspace_root/build/tools/llvm-riscv}"
llvm_root="$work_dir/llvm"
llvm_build="$llvm_root/build"
llvm_config="$llvm_build/lib/cmake/llvm/LLVMConfig.cmake"
output_dir="${RISCV_WAMRC_BUILD_DIR:-$workspace_root/build/tools/wamrc-riscv}"
case "$(uname -s)" in
    Darwin) host_platform="darwin" ;;
    Linux) host_platform="linux" ;;
    *)
        echo "riscv32 wamrc bootstrap supports macOS and Linux hosts." >&2
        exit 2
        ;;
esac

if [[ ! -f "$llvm_config" ]]; then
    rm -rf "$work_dir"
    mkdir -p "$work_dir"
    git clone --depth 1 --branch llvmorg-18.1.8 \
        https://github.com/llvm/llvm-project.git "$llvm_root"
    cmake -S "$llvm_root/llvm" -B "$llvm_build" -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
        -DLLVM_APPEND_VC_REV=ON \
        -DLLVM_BUILD_EXAMPLES=OFF \
        -DLLVM_BUILD_LLVM_DYLIB=OFF \
        -DLLVM_ENABLE_BINDINGS=OFF \
        -DLLVM_ENABLE_IDE=OFF \
        -DLLVM_ENABLE_LIBEDIT=OFF \
        -DLLVM_ENABLE_TERMINFO=OFF \
        -DLLVM_ENABLE_ZLIB=OFF \
        -DLLVM_ENABLE_ZSTD=OFF \
        -DLLVM_ENABLE_LIBXML2=OFF \
        -DLLVM_INCLUDE_BENCHMARKS=OFF \
        -DLLVM_INCLUDE_DOCS=OFF \
        -DLLVM_INCLUDE_EXAMPLES=OFF \
        -DLLVM_INCLUDE_TESTS=OFF \
        -DLLVM_INCLUDE_TOOLS=OFF \
        -DLLVM_INCLUDE_UTILS=OFF \
        -DLLVM_OPTIMIZED_TABLEGEN=ON \
        -DLLVM_TARGETS_TO_BUILD=RISCV \
        -DLLVM_USE_PERF=ON
    # Build the installable package (self-contained: LLVMConfig.cmake ships
    # its own modules, no source tree needed afterwards).
    cmake --build "$llvm_build" --target package --parallel 2
    # Repackage: move LLVM-*.tar.gz out, wipe build, unpack into build.
    pack="$(ls "$llvm_build"/LLVM-*.tar.gz | head -1)"
    mv "$pack" "$work_dir/"
    rm -rf "$llvm_build"
    mkdir -p "$llvm_build"
    tar xf "$work_dir/$(basename "$pack")" --strip-components=1 -C "$llvm_build"
    rm -f "$work_dir/$(basename "$pack")"
fi

if [[ ! -f "$llvm_config" ]]; then
    echo "riscv32 LLVM build completed without $llvm_config" >&2
    exit 1
fi

rm -rf "$output_dir"
cmake -S "$wamr_root/wamr-compiler" -B "$output_dir" \
    -DCMAKE_BUILD_TYPE=Release \
    -DWAMR_BUILD_PLATFORM="$host_platform" \
    -DWAMR_BUILD_WITH_CUSTOM_LLVM=1 \
    -DWAMR_BUILD_SIMD=0 \
    -DLLVM_DIR="$(dirname "$llvm_config")"
cmake --build "$output_dir" --parallel 2

"$output_dir/wamrc" --version
echo "riscv32-capable wamrc ready: $output_dir/wamrc"
