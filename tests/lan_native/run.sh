#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
bin=$(mktemp "${TMPDIR:-/tmp}/pw-lan-gate.XXXXXX")
trap 'rm -f "$bin"' EXIT HUP INT TERM
${CC:-cc} -std=c11 -Wall -Wextra -Werror -pedantic -fsanitize=address,undefined \
 -I"$root/firmware/components/pw_app/include" "$root/firmware/components/pw_app/pw_lan_gate.c" \
 "$root/tests/lan_native/test_gate.c" -o "$bin"
UBSAN_OPTIONS=halt_on_error=1 "$bin"
