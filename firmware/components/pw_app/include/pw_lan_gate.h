#pragma once
#include <stdbool.h>
#include <stdint.h>
typedef struct { int64_t until_us; uint32_t peer; unsigned attempts; bool paired; char code[7]; } pw_lan_gate_t;
void pw_lan_gate_open(pw_lan_gate_t *, int64_t now, uint32_t random);
void pw_lan_gate_close(pw_lan_gate_t *);
bool pw_lan_gate_pair(pw_lan_gate_t *, int64_t now, uint32_t peer, const char *code);
bool pw_lan_gate_allows(const pw_lan_gate_t *, int64_t now, uint32_t peer);
