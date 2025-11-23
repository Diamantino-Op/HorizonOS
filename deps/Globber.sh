#!/bin/sh

dir=${1:-.}

if [ ! -d "$dir" ]; then
  echo "Directory not found: $dir" >&2
  exit 1
fi

find "$dir" -type f \( -name '*.cpp' -o -name '*.c' -o -name '*.s' \) | while IFS= read -r file; do
  echo "$file"
done
