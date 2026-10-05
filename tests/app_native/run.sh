#!/bin/sh
set -eu
repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
json_dir=${CJSON_DIR:-${IDF_PATH:+$IDF_PATH/components/json/cJSON}}
if [ -z "$json_dir" ] || [ ! -f "$json_dir/cJSON.c" ]; then
  printf '%s\n' 'Set IDF_PATH to ESP-IDF or CJSON_DIR to its cJSON source directory.' >&2
  exit 1
fi
build_dir=$(mktemp -d "${TMPDIR:-/tmp}/pw-app-native.XXXXXX")
trap 'rm -rf "$build_dir"' EXIT HUP INT TERM
"${CC:-cc}" -std=c11 -fsanitize=address,undefined -fno-omit-frame-pointer \
  -Wno-deprecated-declarations -I"$json_dir" -c "$json_dir/cJSON.c" -o "$build_dir/cJSON.o"
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -pedantic -fsanitize=address,undefined -fno-omit-frame-pointer \
  -I"$repo_dir/firmware/components/pw_app/include" -I"$json_dir" \
  "$repo_dir/firmware/components/pw_app/pw_validation.c" \
  "$repo_dir/tests/app_native/test_validation.c" "$build_dir/cJSON.o" \
  -lm -o "$build_dir/test_validation"
UBSAN_OPTIONS=halt_on_error=1 "$build_dir/test_validation"
