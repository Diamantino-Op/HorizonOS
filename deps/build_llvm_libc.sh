#!/bin/sh

set -eu

script_dir=$(cd "$(dirname "$0")" && pwd)
repo_root=$(cd "$script_dir/.." && pwd)

mlibc_branch=${MLIBC_BRANCH:-horizonos-mlibc}
default_llvm_branch=${LLVM_BRANCH:-horizonos_llvm}

usage() {
  echo "Usage: $0 {setup-repos|build-llvm|build-libc} [arch] [llvm-branch] [mlibc-buildtype]" >&2
  exit 1
}

ensure_repo() {
  url=$1
  branch=$2
  path=$3

  if git -C "$path" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
    git -C "$path" remote set-url origin "$url"
    git -C "$path" fetch --depth 1 origin "$branch"
    git -C "$path" checkout -B "$branch" "origin/$branch"
    git -C "$path" reset --hard "origin/$branch"
    git -C "$path" clean -fdx
  else
    rm -rf "$path"
    git clone --single-branch --depth 1 -b "$branch" "$url" "$path"
  fi
}

ensure_repos() {
  llvm_branch=$1

  ensure_repo https://github.com/Diamantino-Op/mlibc.git "$mlibc_branch" "$repo_root/libs/mlibc"
  ensure_repo https://github.com/Diamantino-Op/llvm-project.git "$llvm_branch" "$repo_root/deps/llvm"
}

build_llvm() {
  arch=$1
  llvm_branch=$2

  ensure_repos "$llvm_branch"

  llvm_path=$repo_root/deps/llvm
  toolchain_path=$repo_root/toolchain
  sysroot_path=$repo_root/libs/sysroot
  llvm_build_path=$llvm_path/build

  cmake -S "$llvm_path/llvm" -B "$llvm_build_path" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_FLAGS=-pipe \
    -DCMAKE_CXX_FLAGS=-pipe \
    -DCMAKE_ASM_FLAGS=-pipe \
    '-DLLVM_ENABLE_PROJECTS=clang;clang-tools-extra;lld' \
    '-DLLVM_TARGETS_TO_BUILD=X86;RISCV;AArch64' \
    "-DCMAKE_INSTALL_PREFIX=$toolchain_path" \
    "-DDEFAULT_SYSROOT=$sysroot_path" \
    "-DLLVM_DEFAULT_TARGET_TRIPLE=$arch-horizonos-elf" \
    -DENABLE_LINKER_BUILD_ID=ON \
    -DLLVM_CCACHE_BUILD=ON \
    -DLLVM_LINK_LLVM_DYLIB=ON \
    -DLLVM_PARALLEL_COMPILE_JOBS=20 \
    -DLLVM_PARALLEL_LINK_JOBS=20

  cmake --build "$llvm_build_path" --parallel
  cmake --install "$llvm_build_path"
  rm -rf "$llvm_build_path"
}

build_libc() {
  arch=$1
  llvm_branch=$2
  mlibc_buildtype=$3

  ensure_repos "$llvm_branch"

  llvm_path=$repo_root/deps/llvm
  compiler_rt_path=$llvm_path/compiler-rt
  runtimes_path=$llvm_path/runtimes
  compiler_rt_build_path=$compiler_rt_path/build-rt-builtins
  cxx_build_path=$compiler_rt_path/build-cxx
  toolchain_path=$repo_root/toolchain
  sysroot_root=$repo_root/libs/sysroot
  sysroot_path=$sysroot_root/usr
  bin_path=$toolchain_path/bin
  mlibc_path=$repo_root/libs/mlibc
  linux_headers_script=$repo_root/deps/setup_linux_headers.sh
  res_file_path=$repo_root/res/$arch/horizon-mlibc-cross.cfg
  sh "$linux_headers_script" "$arch"
  linux_headers_path=$repo_root/deps/linux-headers-$arch/include

  cd "$mlibc_path"
  rm -rf build
  meson setup build --cross-file "$res_file_path" --prefix "$sysroot_path/lib" --libdir "$sysroot_path/lib" --includedir "$sysroot_path/include" -Ddefault_library=both --buildtype "$mlibc_buildtype" -Dlinux_kernel_headers="$linux_headers_path" -Dheaders_only=true
  cd build
  meson compile
  meson install
  cd ..
  rm -rf build

  cmake -S "$compiler_rt_path" -B "$compiler_rt_build_path" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER="$bin_path/clang" \
    -DCMAKE_CXX_COMPILER="$bin_path/clang++" \
    -DCMAKE_ASM_COMPILER="$bin_path/clang" \
    -DCMAKE_AR="$bin_path/llvm-ar" \
    -DCMAKE_RANLIB="$bin_path/llvm-ranlib" \
    -DCMAKE_NM="$bin_path/llvm-nm" \
    "-DLLVM_CMAKE_DIR=$toolchain_path/lib/cmake/llvm" \
    "-DCMAKE_SYSROOT=$sysroot_root" \
    "-DCMAKE_INSTALL_PREFIX=$sysroot_path" \
    "-DCMAKE_C_COMPILER_TARGET=$arch-horizonos-elf" \
    "-DCMAKE_CXX_COMPILER_TARGET=$arch-horizonos-elf" \
    "-DCMAKE_ASM_COMPILER_TARGET=$arch-horizonos-elf" \
    "-DCMAKE_C_FLAGS=-fuse-ld=lld -pipe" \
    "-DCMAKE_CXX_FLAGS=-fuse-ld=lld -pipe" \
    "-DCMAKE_ASM_FLAGS=-fuse-ld=lld -pipe" \
    -DCMAKE_EXE_LINKER_FLAGS=-fuse-ld=lld \
    -DCOMPILER_RT_BUILD_BUILTINS=ON \
    -DCOMPILER_RT_BUILD_SANITIZERS=OFF \
    -DCOMPILER_RT_BUILD_XRAY=OFF \
    -DCOMPILER_RT_BUILD_LIBFUZZER=OFF \
    -DCOMPILER_RT_BUILD_PROFILE=OFF \
    -DCOMPILER_RT_BUILD_CRT=ON \
    -DCOMPILER_RT_DEFAULT_TARGET_ONLY=ON \
    -DCOMPILER_RT_INCLUDE_TESTS=OFF \
    -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY \
    -DCOMPILER_RT_BUILD_STANDALONE_LIBATOMIC=ON \
    -DLLVM_PARALLEL_COMPILE_JOBS=20 \
    -DLLVM_PARALLEL_LINK_JOBS=20

  cmake --build "$compiler_rt_build_path" --target builtins --parallel
  cmake --build "$compiler_rt_build_path" --target install-builtins
  ln -sr "$sysroot_path/lib/linux/libclang_rt.builtins-$arch.a" "$sysroot_path/lib/libgcc.a"

  clang_resource_dir=$("$bin_path/clang" --print-resource-dir)
  mkdir -p "$clang_resource_dir/lib/$arch-unknown-horizonos-elf"
  ln -sr "$sysroot_path/lib/linux/libclang_rt.builtins-$arch.a" "$clang_resource_dir/lib/$arch-unknown-horizonos-elf/libclang_rt.builtins.a"

  cmake --build "$compiler_rt_build_path" --target crt --parallel
  cmake --build "$compiler_rt_build_path" --target install-crt
  ln -sr "$sysroot_path/lib/linux/clang_rt.crtbegin-$arch.o" "$sysroot_path/lib/crtbegin.o"
  ln -sr "$sysroot_path/lib/linux/clang_rt.crtend-$arch.o" "$sysroot_path/lib/crtend.o"
  ln -sr "$sysroot_path/lib/linux/clang_rt.crtbegin-$arch.o" "$sysroot_path/lib/crtbeginS.o"
  ln -sr "$sysroot_path/lib/linux/clang_rt.crtend-$arch.o" "$sysroot_path/lib/crtendS.o"
  ln -sr "$sysroot_path/lib/linux/clang_rt.crtbegin-$arch.o" "$sysroot_path/lib/crtbeginT.o"
  ln -sr "$sysroot_path/lib/linux/clang_rt.crtend-$arch.o" "$sysroot_path/lib/crtendT.o"

  cmake --build "$compiler_rt_build_path" --target "clang_rt.atomic-dynamic-$arch" --parallel
  cmake --build "$compiler_rt_build_path" --target "install-clang_rt.atomic-dynamic-$arch"
  ln -sr "$sysroot_path/lib/linux/libclang_rt.atomic-$arch.so" "$sysroot_path/lib/libatomic.so"

  cd "$mlibc_path"
  rm -rf build
  meson setup build --cross-file "$res_file_path" --prefix "$sysroot_path/lib" --libdir "$sysroot_path/lib" --includedir "$sysroot_path/include" -Ddefault_library=both --buildtype "$mlibc_buildtype" -Dlinux_kernel_headers="$linux_headers_path" -Dno_headers=true
  cd build
  meson compile
  meson install
  cd ..
  rm -rf build

  cmake -S "$runtimes_path" -B "$cxx_build_path" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER="$bin_path/clang" \
    -DCMAKE_CXX_COMPILER="$bin_path/clang++" \
    -DCMAKE_ASM_COMPILER="$bin_path/clang" \
    "-DCMAKE_SYSROOT=$sysroot_root" \
    "-DCMAKE_INSTALL_PREFIX=$sysroot_path" \
    "-DCMAKE_C_COMPILER_TARGET=$arch-horizonos-elf" \
    "-DCMAKE_CXX_COMPILER_TARGET=$arch-horizonos-elf" \
    "-DCMAKE_ASM_COMPILER_TARGET=$arch-horizonos-elf" \
    "-DLLVM_RUNTIME_TARGETS=$arch-horizonos-elf" \
    '-DLLVM_ENABLE_RUNTIMES=libunwind;libcxxabi;libcxx' \
    "-DCMAKE_C_FLAGS=-fuse-ld=lld -pipe" \
    "-DCMAKE_CXX_FLAGS=-fuse-ld=lld -pipe" \
    "-DCMAKE_ASM_FLAGS=-fuse-ld=lld -pipe" \
    -DCMAKE_EXE_LINKER_FLAGS=-fuse-ld=lld \
    -DLLVM_ENABLE_PER_TARGET_RUNTIME_DIR=OFF \
    -DLIBCXX_CXX_ABI=libcxxabi \
    -DLIBCXXABI_USE_LLVM_UNWINDER=ON \
    -DLIBCXX_ENABLE_SHARED=ON \
    -DLIBCXX_ENABLE_STATIC=ON \
    -DLIBCXXABI_ENABLE_SHARED=ON \
    -DLIBCXXABI_ENABLE_STATIC=ON \
    -DLIBUNWIND_ENABLE_SHARED=ON \
    -DLIBUNWIND_ENABLE_STATIC=ON \
    -DLIBCXX_ENABLE_ASSERTIONS=ON \
    -DLIBCXX_INCLUDE_TESTS=OFF \
    -DLIBCXXABI_INCLUDE_TESTS=OFF \
    -DLIBUNWIND_INCLUDE_TESTS=OFF \
    -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY \
    -DLIBCXX_ENABLE_THREADS=ON \
    -DLIBCXXABI_ENABLE_THREADS=ON \
    -DLIBCXX_HAS_PTHREAD_API=ON \
    -DLIBCXXABI_HAS_PTHREAD_API=ON \
    -DLIBCXX_ENABLE_LOCALIZATION=OFF \
    -DLIBCXX_ENABLE_WIDE_CHARACTERS=OFF \
    -DLLVM_PARALLEL_COMPILE_JOBS=20 \
    -DLLVM_PARALLEL_LINK_JOBS=20

  cmake --build "$cxx_build_path" --target unwind cxxabi cxx --parallel
  cmake --build "$cxx_build_path" --target install-unwind install-cxxabi install-cxx
  rm -rf "$compiler_rt_build_path"
  rm -rf "$cxx_build_path"
}

case ${1:-} in
  setup-repos)
    llvm_branch=${2:-$default_llvm_branch}
    ensure_repos "$llvm_branch"
    ;;
  build-llvm)
    [ $# -ge 3 ] || usage
    build_llvm "$2" "$3"
    ;;
  build-libc)
    [ $# -ge 4 ] || usage
    build_libc "$2" "$3" "$4"
    ;;
  *)
    usage
    ;;
esac



