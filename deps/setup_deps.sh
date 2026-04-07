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

[ $# -eq 6 ] || {
  echo "Usage: $0 <chdrs-branch> <cxxhdrs-branch> <flanterm-branch> <limine-branch> <limine-protocol-branch> <uacpi-branch>" >&2
  exit 1
}

chdrs_branch=$1
cxxhdrs_branch=$2
flanterm_branch=$3
limine_branch=$4
limine_protocol_branch=$5
uacpi_branch=$6

ensure_repo https://codeberg.org/OSDev/freestnd-c-hdrs.git "$chdrs_branch" "$repo_root/deps/chdrs"
ensure_repo https://codeberg.org/OSDev/freestnd-cxx-hdrs.git "$cxxhdrs_branch" "$repo_root/deps/cxxhdrs"
ensure_repo https://codeberg.org/Mintsuki/Flanterm.git "$flanterm_branch" "$repo_root/deps/flanterm"
ensure_repo https://codeberg.org/Limine/Limine.git "$limine_branch" "$repo_root/deps/limine"
ensure_repo https://codeberg.org/Limine/limine-protocol.git "$limine_protocol_branch" "$repo_root/deps/limine_protocol"
ensure_repo https://github.com/uACPI/uACPI.git "$uacpi_branch" "$repo_root/deps/uacpi"

