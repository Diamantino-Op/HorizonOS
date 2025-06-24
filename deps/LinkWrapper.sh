#!/bin/bash

LINKER="$1"
shift

args=()

for arg in "$@"; do
    [[ "$arg" == --dependency-file=* ]] && continue
    [[ "$arg" == -Xlinker ]] && continue

    args+=("$arg")
done

exec "$LINKER" "${args[@]}"