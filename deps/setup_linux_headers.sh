#!/bin/sh

set -eu

script_dir=$(cd "$(dirname "$0")" && pwd)
repo_root=$(cd "$script_dir/.." && pwd)

linux_headers_version=${LINUX_HEADERS_VERSION:-6.17.9}
linux_headers_url=${LINUX_HEADERS_URL:-https://cdn.kernel.org/pub/linux/kernel/v6.x/linux-${linux_headers_version}.tar.xz}

[ $# -eq 1 ] || {
  echo "Usage: $0 <arch>" >&2
  exit 1
}

arch=$1
linux_headers_path=$repo_root/deps/linux-headers-$arch

if [ -d "$linux_headers_path" ]; then
  exit 0
fi

linux_kernel_path=$repo_root/deps/linux_kernel
linux_tarball_path=$repo_root/deps/linux-${linux_headers_version}.tar.xz

rm -rf "$linux_kernel_path" "$linux_headers_path"
mkdir -p "$linux_kernel_path"

wget -O "$linux_tarball_path" "$linux_headers_url"
tar xf "$linux_tarball_path" -C "$linux_kernel_path"

cd "$linux_kernel_path/linux-${linux_headers_version}"
make ARCH="$arch" headers_install INSTALL_HDR_PATH="$linux_headers_path"

rm -rf "$linux_tarball_path" "$linux_kernel_path"
