#!/bin/sh

set -eu

script_dir=$(CDPATH= cd "$(dirname "$0")" && pwd)
repo_root=$(CDPATH= cd "$script_dir/.." && pwd)

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

[ $# -eq 7 ] || {
  echo "Usage: $0 <chdrs-branch> <cxxhdrs-branch> <flanterm-branch> <limine-protocol-branch> <uacpi-branch> <tcmalloc-branch> <abseil-cpp-branch>" >&2
  exit 1
}

chdrs_branch=$1
cxxhdrs_branch=$2
flanterm_branch=$3
limine_protocol_branch=$4
uacpi_branch=$5
tcmalloc_branch=$6
abseil_cpp_branch=$7

ensure_repo https://github.com/osdev0/freestnd-c-hdrs.git "$chdrs_branch" "$repo_root/deps/chdrs"
ensure_repo https://github.com/osdev0/freestnd-cxx-hdrs.git "$cxxhdrs_branch" "$repo_root/deps/cxxhdrs"
ensure_repo https://github.com/Mintsuki/Flanterm.git "$flanterm_branch" "$repo_root/deps/flanterm"
ensure_repo https://github.com/Limine-Bootloader/limine-protocol.git "$limine_protocol_branch" "$repo_root/deps/limine_protocol"
ensure_repo https://github.com/uACPI/uACPI.git "$uacpi_branch" "$repo_root/deps/uacpi"
ensure_repo https://github.com/Diamantino-Op/tcmalloc.git "$tcmalloc_branch" "$repo_root/libs/tcmalloc"
ensure_repo https://github.com/Diamantino-Op/abseil-cpp.git "$abseil_cpp_branch" "$repo_root/libs/abseil-cpp"

tag=$(curl -fsSL "https://api.github.com/repos/Limine-Bootloader/Limine/releases/latest" | grep '"tag_name"' | head -1 | sed 's/.*"tag_name": *"\(.*\)".*/\1/')

echo "Latest release: ${tag}"

curl -fsSL -o "$repo_root"/deps/limine-binary.tar.xz "https://github.com/Limine-Bootloader/Limine/releases/download/${tag}/limine-binary.tar.xz"

tar -xf "$repo_root"/deps/limine-binary.tar.xz -C "$repo_root"/deps

extracted_dir=$(tar -tf "$repo_root"/deps/limine-binary.tar.xz | head -1 | cut -d/ -f1)

mv "$repo_root"/deps/"${extracted_dir}" "$repo_root"/deps/limine

rm "$repo_root"/deps/limine-binary.tar.xz

cd "$repo_root"/deps/limine && make

# Build TCMalloc

tcmalloc_build_dir="$repo_root/libs/tcmalloc/out"

cd "$repo_root/libs/tcmalloc"

bazel clean --expunge

bazelisk build --compilation_mode=opt //tcmalloc:tcmalloc --output_groups=+static_library,+dynamic_library --platforms=//platforms:horizonos --spawn_strategy=local --strategy=CppCompile=local --strategy=CppLink=local --define=HORIZON_SYSROOT="$repo_root/libs/sysroot" --define=HORIZON_TOOLCHAIN="$repo_root/toolchain" --action_env=HORIZON_SYSROOT="$repo_root/libs/sysroot" --action_env=HORIZON_TOOLCHAIN="$repo_root/toolchain"

bazel_bin_folder=$(bazelisk info output_path)
lo_file="${bazel_bin_folder}/k8-opt/bin/tcmalloc/libtcmalloc.lo"

mkdir -p "${tcmalloc_build_dir}"

cp "${lo_file}" "${tcmalloc_build_dir}/libtcmalloc.lo"

"$repo_root"/toolchain/bin/llvm-ar rcs "${tcmalloc_build_dir}/libtcmalloc.a" "${lo_file}"