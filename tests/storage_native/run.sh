#!/bin/sh
set -eu
repo=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
storage_out=$(mktemp -d "${TMPDIR:-/tmp}/pw-storage.XXXXXX")
trap 'rm -rf "$storage_out"' EXIT
cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -g \
  -I"$repo/tests/storage_native/stubs" -I"$repo/firmware/components/pw_storage/include" \
  "$repo/tests/storage_native/test_storage.c" -o "$storage_out/test-storage"
for scenario in 0 1 2 3 4 5 6 7 8 9 10 11 12 13; do "$storage_out/test-storage" "$scenario"; done
