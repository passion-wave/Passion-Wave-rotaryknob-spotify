#!/bin/sh
set -eu
component_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
: "${IDF_PATH:?Set IDF_PATH to ESP-IDF 5.4.3}"
json_dir="$IDF_PATH/components/json/cJSON"
mbed_dir="$IDF_PATH/components/mbedtls/mbedtls"
build_dir=$(mktemp -d "${TMPDIR:-/tmp}/pw-spotify-native.XXXXXX")
trap 'rm -rf "$build_dir"' EXIT HUP INT TERM
# A standalone mbedTLS build avoids ESP hardware acceleration in the host tests.
cat > "$build_dir/mbedtls_config.h" <<'CONFIG'
#define MBEDTLS_SHA256_C
#define MBEDTLS_BASE64_C
CONFIG
cc=${CC:-cc}
"$cc" -std=c11 -fsanitize=address,undefined -fno-omit-frame-pointer -Wno-deprecated-declarations \
  -I"$json_dir" -c "$json_dir/cJSON.c" -o "$build_dir/cJSON.o"
"$cc" -std=c11 -fsanitize=address,undefined -fno-omit-frame-pointer \
  -DMBEDTLS_CONFIG_FILE='"mbedtls_config.h"' -I"$build_dir" -I"$mbed_dir/include" -I"$mbed_dir/library" \
  -c "$mbed_dir/library/sha256.c" -o "$build_dir/sha256.o"
"$cc" -std=c11 -fsanitize=address,undefined -fno-omit-frame-pointer \
  -DMBEDTLS_CONFIG_FILE='"mbedtls_config.h"' -I"$build_dir" -I"$mbed_dir/include" -I"$mbed_dir/library" \
  -c "$mbed_dir/library/base64.c" -o "$build_dir/base64.o"
"$cc" -std=c11 -fsanitize=address,undefined -fno-omit-frame-pointer \
  -DMBEDTLS_CONFIG_FILE='"mbedtls_config.h"' -I"$build_dir" -I"$mbed_dir/include" -I"$mbed_dir/library" \
  -c "$mbed_dir/library/platform_util.c" -o "$build_dir/platform_util.o"
"$cc" -std=c11 -Wall -Wextra -Werror -pedantic -fsanitize=address,undefined -fno-omit-frame-pointer \
  -D_POSIX_C_SOURCE=200809L -DMBEDTLS_CONFIG_FILE='"mbedtls_config.h"' -I"$build_dir" -I"$component_dir/tests/stubs" -I"$component_dir/include" -I"$component_dir" -I"$json_dir" -I"$mbed_dir/include" \
  "$component_dir/pw_spotify_model.c" "$component_dir/tests/test_model.c" \
  "$build_dir/cJSON.o" "$build_dir/sha256.o" "$build_dir/base64.o" "$build_dir/platform_util.o" \
  -lm -o "$build_dir/test_model"
UBSAN_OPTIONS=halt_on_error=1 "$build_dir/test_model"
"$cc" -std=c11 -Wall -Wextra -Werror -pedantic -fsanitize=address,undefined -fno-omit-frame-pointer \
  -D_POSIX_C_SOURCE=200809L -DMBEDTLS_CONFIG_FILE='"mbedtls_config.h"' \
  -I"$build_dir" -I"$component_dir/tests/stubs" -I"$component_dir/include" -I"$component_dir" -I"$json_dir" -I"$mbed_dir/include" \
  "$component_dir/pw_spotify_model.c" "$component_dir/tests/test_worker.c" \
  "$build_dir/cJSON.o" "$build_dir/sha256.o" "$build_dir/base64.o" "$build_dir/platform_util.o" \
  -lm -o "$build_dir/test_worker"
UBSAN_OPTIONS=halt_on_error=1 "$build_dir/test_worker"
