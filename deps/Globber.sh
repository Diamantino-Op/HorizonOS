#!/bin/sh

if [ $# -gt 0 ] && [ -d "$1" ]; then
  dir=$1
  shift
else
  dir=.
fi

if [ ! -d "$dir" ]; then
  echo "Directory not found: $dir" >&2
  exit 1
fi

if [ $# -eq 0 ]; then
  set -- '*.cpp' '*.c' '*.s'
fi

names=''
first=1
for p in "$@"; do
  if [ $first -eq 1 ]; then
    names="$names -name '$p'"
    first=0
  else
    names="$names -o -name '$p'"
  fi
done

# esegue find con i pattern
eval "find \"\$dir\" -type f \\( $names \\) -print" | while IFS= read -r file; do
  echo "$file"
done
