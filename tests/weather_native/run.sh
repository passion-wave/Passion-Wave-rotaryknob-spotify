#!/bin/sh
set -eu
repo=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
weather_idf=${IDF_PATH:-"$HOME/esp/esp-idf-v5.4.3"}
weather_out=$(mktemp -d "${TMPDIR:-/tmp}/pw-weather-tests.XXXXXX")
trap 'rm -rf "$weather_out"' EXIT
for source in "$repo/tests/weather_native/test_weather.c" \
  "$repo/firmware/components/pw_weather/pw_weather_model.c" \
  "$repo/firmware/components/pw_weather/pw_weather_inflate.c" \
  "$weather_idf/components/json/cJSON/cJSON.c"; do
  object=$(basename "$source" .c)
  cc -std=c11 -D_POSIX_C_SOURCE=200809L -DPW_WEATHER_HOST -Wall -Wextra -Werror \
    -fsanitize=address,undefined -g \
    -I"$repo/firmware/components/pw_weather/include" \
    -I"$repo/firmware/components/pw_weather" -I"$weather_idf/components/json/cJSON" \
    -c "$source" -o "$weather_out/$object.o"
done
c++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -g \
  -I"$repo/firmware/components/pw_weather/include" \
  -c "$repo/firmware/components/pw_weather/pw_weather_avatar.cpp" -o "$weather_out/avatar.o"
c++ -fsanitize=address,undefined "$weather_out/"*.o -lz -lm -o "$weather_out/test-weather"
"$weather_out/test-weather" "$repo/tests/weather_native/met_berlin_20260930.json"
