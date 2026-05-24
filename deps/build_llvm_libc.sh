#!/bin/sh

set -eu

script_dir=$(cd "$(dirname "$0")" && pwd)
repo_root=$(cd "$script_dir/.." && pwd)

mlibc_branch=${MLIBC_BRANCH:-horizonos-mlibc}
default_llvm_branch=${LLVM_BRANCH:-horizonos_llvm}

usage() {
  echo "Usage: $0 [--skip-repo-check|-s] {setup-repos|build-llvm|build-libc} [arch] [llvm-branch] [mlibc-buildtype]" >&2
  exit 1
}

ensure_repo() {
  url=$1
  branch=$2
  path=$3

  if git -C "$path" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
    git -C "$path" remote set-url origin "$url"
    # Try to fetch the requested branch, but don't fail the script if fetch has issues
    git -C "$path" fetch --depth 1 origin "$branch" || true

    # Only perform destructive updates (reset --hard, removing build dirs) when it's
    # safe to do so: the working tree must be clean and the local branch must be
    # behind (fast-forwardable) the remote branch. If there are uncommitted
    # changes, local commits, or the branch has diverged, skip the reset to
    # preserve the user's local edits.
    if git -C "$path" rev-parse --verify "origin/$branch" >/dev/null 2>&1; then
      if [ -n "$(git -C "$path" status --porcelain 2>/dev/null)" ]; then
        echo "Repository $path has uncommitted changes; skipping destructive update to preserve local edits."
      else
        head=$(git -C "$path" rev-parse --verify HEAD 2>/dev/null || true)
        remote_head=$(git -C "$path" rev-parse --verify "origin/$branch" 2>/dev/null || true)
        base=$(git -C "$path" merge-base HEAD "origin/$branch" 2>/dev/null || true)

        if [ "$head" = "$remote_head" ]; then
          # Already up-to-date with remote
          git -C "$path" checkout -B "$branch" "origin/$branch"
        elif [ "$base" = "$remote_head" ]; then
          # Remote is ancestor of local -> local has commits that would be lost by a reset
          echo "Repository $path has local commits not present on origin/$branch; skipping destructive update to preserve local commits."
        elif [ "$base" = "$head" ]; then
          # Local is ancestor of remote -> safe to fast-forward
          echo "Repository $path is behind origin/$branch; fast-forwarding to update."
          git -C "$path" checkout -B "$branch" "origin/$branch"
          git -C "$path" reset --hard "origin/$branch"
          rm -rf "$path/build" "$path/build-cxx" "$path/build-rt-builtins"
        else
          # Diverged
          echo "Repository $path has diverged from origin/$branch; skipping update to preserve local changes."
        fi
      fi
    else
      echo "Remote branch origin/$branch not found for $path; leaving repository unchanged."
    fi
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

  if [ -z "${SKIP_REPO_CHECK:-}" ]; then
    ensure_repos "$llvm_branch"
  else
    echo "Skipping repository setup (ensure_repos) because SKIP_REPO_CHECK is set."
  fi

  llvm_path=$repo_root/deps/llvm
  toolchain_path=$repo_root/toolchain
  sysroot_path=$repo_root/libs/sysroot
  llvm_build_path=$llvm_path/build

  cmake -S "$llvm_path/llvm" -B "$llvm_build_path" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_FLAGS=-pipe \
    -DCMAKE_CXX_FLAGS=-pipe \
    -DCMAKE_ASM_FLAGS=-pipe \
    '-DLLVM_ENABLE_PROJECTS="clang;clang-tools-extra;lld"' \
    '-DLLVM_TARGETS_TO_BUILD="X86;RISCV;AArch64"' \
    "-DCMAKE_INSTALL_PREFIX=$toolchain_path" \
    "-DDEFAULT_SYSROOT=$sysroot_path" \
    "-DLLVM_DEFAULT_TARGET_TRIPLE=$arch-horizonos" \
    -DENABLE_LINKER_BUILD_ID=ON \
    -DLLVM_CCACHE_BUILD=ON \
    -DLLVM_LINK_LLVM_DYLIB=ON \
    -DCLANG_DEFAULT_CXX_STDLIB=libc++ \
    -DCLANG_DEFAULT_RTLIB=compiler-rt \
    -DCLANG_DEFAULT_UNWINDLIB=libunwind \
    -DCLANG_DEFAULT_LINKER=lld \
    -DLLVM_PARALLEL_COMPILE_JOBS=20 \
    -DLLVM_PARALLEL_LINK_JOBS=20

  cmake --build "$llvm_build_path" --parallel
  cmake --install "$llvm_build_path"
  #rm -rf "$llvm_build_path"
}

build_libc() {
  arch=$1
  llvm_branch=$2
  mlibc_buildtype=$3

  if [ -z "${SKIP_REPO_CHECK:-}" ]; then
    ensure_repos "$llvm_branch"
  else
    echo "Skipping repository setup (ensure_repos) because SKIP_REPO_CHECK is set."
  fi

  llvm_path=$repo_root/deps/llvm
  compiler_rt_path=$llvm_path/compiler-rt
  runtimes_path=$llvm_path/runtimes
  compiler_rt_build_path=$llvm_path/build-rt-builtins
  cxx_build_path=$llvm_path/build-cxx
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
    "-DCMAKE_C_COMPILER_TARGET=$arch-horizonos" \
    "-DCMAKE_CXX_COMPILER_TARGET=$arch-horizonos" \
    "-DCMAKE_ASM_COMPILER_TARGET=$arch-horizonos" \
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
    -DLLVM_ENABLE_PER_TARGET_RUNTIME_DIR=ON \
    -DLLVM_PARALLEL_COMPILE_JOBS=20 \
    -DLLVM_PARALLEL_LINK_JOBS=20

  cmake --build "$compiler_rt_build_path" --target builtins --parallel
  cmake --build "$compiler_rt_build_path" --target install-builtins
  ln -sr "$sysroot_path/lib/$arch-unknown-horizonos/libclang_rt.builtins.a" "$sysroot_path/lib/libgcc.a"

  clang_resource_dir=$("$bin_path/clang" --print-resource-dir)
  mkdir -p "$clang_resource_dir/lib/$arch-unknown-horizonos-elf"
  mkdir -p "$clang_resource_dir/lib/$arch-unknown-horizonos"
  ln -sr "$sysroot_path/lib/$arch-unknown-horizonos/libclang_rt.builtins.a" "$clang_resource_dir/lib/$arch-unknown-horizonos-elf/libclang_rt.builtins.a"
  ln -sr "$sysroot_path/lib/$arch-unknown-horizonos/libclang_rt.builtins.a" "$clang_resource_dir/lib/$arch-unknown-horizonos/libclang_rt.builtins.a"

  cd "$mlibc_path"
  rm -rf build
  meson setup build --cross-file "$res_file_path" --prefix "$sysroot_path/lib" --libdir "$sysroot_path/lib" --includedir "$sysroot_path/include" -Ddefault_library=both --buildtype "$mlibc_buildtype" -Dlinux_kernel_headers="$linux_headers_path" -Dno_headers=true
  cd build
  meson compile
  meson install
  cd ..
  rm -rf build

  cmake --build "$compiler_rt_build_path" --target crt --parallel
  cmake --build "$compiler_rt_build_path" --target install-crt
  ln -sr "$sysroot_path/lib/$arch-unknown-horizonos/clang_rt.crtbegin.o" "$sysroot_path/lib/crtbegin.o"
  ln -sr "$sysroot_path/lib/$arch-unknown-horizonos/clang_rt.crtend.o" "$sysroot_path/lib/crtend.o"
  ln -sr "$sysroot_path/lib/$arch-unknown-horizonos/clang_rt.crtbegin.o" "$sysroot_path/lib/crtbeginS.o"
  ln -sr "$sysroot_path/lib/$arch-unknown-horizonos/clang_rt.crtend.o" "$sysroot_path/lib/crtendS.o"
  ln -sr "$sysroot_path/lib/$arch-unknown-horizonos/clang_rt.crtbegin.o" "$sysroot_path/lib/crtbeginT.o"
  ln -sr "$sysroot_path/lib/$arch-unknown-horizonos/clang_rt.crtend.o" "$sysroot_path/lib/crtendT.o"

  cmake --build "$compiler_rt_build_path" --target "clang_rt.atomic-dynamic-$arch" --parallel
  cmake --build "$compiler_rt_build_path" --target "install-clang_rt.atomic-dynamic-$arch"
  ln -sr "$sysroot_path/lib/$arch-unknown-horizonos/libclang_rt.atomic.so" "$sysroot_path/lib/libatomic.so"

  cmake -S "$runtimes_path" -B "$cxx_build_path" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER="$bin_path/clang" \
    -DCMAKE_CXX_COMPILER="$bin_path/clang++" \
    -DCMAKE_ASM_COMPILER="$bin_path/clang" \
    "-DCMAKE_SYSROOT=$sysroot_root" \
    "-DCMAKE_INSTALL_PREFIX=$sysroot_path" \
    "-DCMAKE_C_COMPILER_TARGET=$arch-horizonos" \
    "-DCMAKE_CXX_COMPILER_TARGET=$arch-horizonos" \
    "-DCMAKE_ASM_COMPILER_TARGET=$arch-horizonos" \
    "-DLLVM_RUNTIME_TARGETS=$arch-horizonos" \
    '-DLLVM_ENABLE_RUNTIMES="libunwind;libcxxabi;libcxx"' \
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
    -DLIBCXX_ENABLE_LOCALIZATION=ON \
    -DLIBCXX_ENABLE_WIDE_CHARACTERS=ON \
    -DLLVM_PARALLEL_COMPILE_JOBS=20 \
    -DLLVM_PARALLEL_LINK_JOBS=20

  cmake --build "$cxx_build_path" --target unwind cxxabi cxx --parallel
  cmake --build "$cxx_build_path" --target install-unwind install-cxxabi install-cxx
  #rm -rf "$compiler_rt_build_path"
  #rm -rf "$cxx_build_path"
}

# Parse global flags (must come before the subcommand).
# Supports: --skip-repo-check, -s
while [ "${1:-}" = "--skip-repo-check" ] || [ "${1:-}" = "-s" ]; do
  SKIP_REPO_CHECK=1
  shift
done

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



