#include "pw_lan_gate.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void) {
    pw_lan_gate_t gate = {0};
    assert(!pw_lan_gate_allows(&gate, 0, 1));
    assert(!pw_lan_gate_pair(&gate, 1, 1, "000000"));
    pw_lan_gate_open(&gate, 100, 123456);
    assert(!strcmp(gate.code, "123456"));
    assert(!pw_lan_gate_allows(&gate, 101, 1));
    assert(!pw_lan_gate_pair(&gate, 101, 1, "12345"));
    assert(!pw_lan_gate_pair(&gate, 101, 1, "123457"));
    assert(pw_lan_gate_pair(&gate, 101, 1, "123456"));
    assert(!gate.code[0]);
    assert(!pw_lan_gate_pair(&gate, 102, 2, "123456"));
    assert(pw_lan_gate_allows(&gate, 102, 1));
    assert(!pw_lan_gate_allows(&gate, 102, 2));
    assert(!pw_lan_gate_allows(&gate, 600000100, 1));
    pw_lan_gate_open(&gate, 200, 42);
    assert(!pw_lan_gate_allows(&gate, 201, 1));
    for (unsigned i=0;i<5;i++) assert(!pw_lan_gate_pair(&gate, 201, 1, "999999"));
    assert(!pw_lan_gate_pair(&gate, 202, 1, "000042"));
    pw_lan_gate_open(&gate, 300, 42);
    assert(!pw_lan_gate_pair(&gate, 301, 0, "000042"));
    assert(pw_lan_gate_pair(&gate, 301, 2, "000042"));
    pw_lan_gate_close(&gate);
    assert(!pw_lan_gate_allows(&gate, 302, 2));
    assert(!gate.code[0] && !gate.until_us && !gate.peer);
    puts("Pilot LAN gate: expiry, physical renewal, peer binding, one-time code, attempt cap and close PASS");
}
