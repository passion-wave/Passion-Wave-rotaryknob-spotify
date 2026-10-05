/* Opt-in pilot HTTP access: physical one-time code, fixed lifetime, one peer.
 * This is authorization, NOT transport encryption or a product trust solution. */
#include "pw_lan_gate.h"
#include <stdio.h>
#include <string.h>
void pw_lan_gate_close(pw_lan_gate_t *g) { memset(g, 0, sizeof(*g)); }
void pw_lan_gate_open(pw_lan_gate_t *g, int64_t now, uint32_t random) {
    pw_lan_gate_close(g); g->until_us = now + 600000000LL;
    snprintf(g->code, sizeof(g->code), "%06lu", (unsigned long)(random % 1000000));
}
bool pw_lan_gate_pair(pw_lan_gate_t *g, int64_t now, uint32_t peer, const char *code) {
    if (now >= g->until_us || !peer || g->paired || g->attempts >= 5) return false;
    g->attempts++;
    if (!code || strlen(code) != 6) return false;
    unsigned different = 0;
    for (unsigned i = 0; i < 6; i++) different |= (unsigned char)code[i] ^ (unsigned char)g->code[i];
    if (different) return false;
    g->paired = true; g->peer = peer; memset(g->code, 0, sizeof(g->code)); return true;
}
bool pw_lan_gate_allows(const pw_lan_gate_t *g, int64_t now, uint32_t peer) {
    return peer && g->paired && now < g->until_us && peer == g->peer;
}
