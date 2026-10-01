#!/bin/sh
set -eu
repo=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
json_dir=${CJSON_DIR:-${IDF_PATH:+$IDF_PATH/components/json/cJSON}}
if [ -z "$json_dir" ] || [ ! -f "$json_dir/cJSON.c" ]; then
    printf '%s\n' 'Set IDF_PATH or CJSON_DIR to the pinned ESP-IDF cJSON directory.' >&2
    exit 1
fi
setup_out=$(mktemp -d "${TMPDIR:-/tmp}/pw-setup-native.XXXXXX")
trap 'rm -rf "$setup_out"' EXIT HUP INT TERM
cc -std=c11 -fsanitize=address,undefined -Wno-deprecated-declarations \
  -I"$json_dir" -c "$json_dir/cJSON.c" -o "$setup_out/cJSON.o"
cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -g \
  -I"$repo/firmware/components/pw_setup_usb/include" \
  -I"$repo/firmware/components/pw_app/include" -I"$json_dir" \
  "$repo/firmware/components/pw_setup_usb/pw_setup_protocol.c" \
  "$repo/firmware/components/pw_app/pw_validation.c" "$setup_out/cJSON.o" \
  "$repo/tests/setup_native/test_protocol.c" -lm -o "$setup_out/test-setup"
UBSAN_OPTIONS=halt_on_error=1 "$setup_out/test-setup"
