#!/bin/sh
set -eu
repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
http_out=$(mktemp -d "${TMPDIR:-/tmp}/pw-http-native.XXXXXX")
trap 'rm -rf "$http_out"' EXIT HUP INT TERM
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -pedantic \
  -fsanitize=address,undefined -fno-omit-frame-pointer -g \
  -I"$repo_dir/firmware/components/pw_app/include" \
  "$repo_dir/firmware/components/pw_app/pw_http_socket.c" \
  "$repo_dir/tests/http_native/test_http_socket.c" -o "$http_out/test-http-socket"
UBSAN_OPTIONS=halt_on_error=1 "$http_out/test-http-socket"
