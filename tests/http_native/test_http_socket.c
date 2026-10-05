// SPDX-License-Identifier: MIT
// Actual production helpers, synthetic addresses and real loopback-only sockets.
#include "pw_http_socket.h"
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/tcp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static unsigned checks;
#define CHECK(expression) do { \
    checks++; \
    if (!(expression)) { \
        fprintf(stderr, "%s:%d: %s failed (errno=%d)\n", __FILE__, __LINE__, \
                #expression, errno); \
        exit(EXIT_FAILURE); \
    } \
} while (0)

static struct in_addr ipv4(const char *text) {
    struct in_addr result;
    CHECK(inet_pton(AF_INET, text, &result) == 1);
    return result;
}

static struct sockaddr_in6 ipv6(const char *text) {
    struct sockaddr_in6 result = {0};
    result.sin6_family = AF_INET6;
#ifdef __APPLE__
    result.sin6_len = sizeof result;
#endif
    CHECK(inet_pton(AF_INET6, text, &result.sin6_addr) == 1);
    return result;
}

static void check_truncated(const void *address, size_t size) {
    for (size_t length = 0; length < size; length++) {
        // Exact allocation makes an out-of-bounds family/address read visible
        // to ASan even when the production helper is handed a short length.
        unsigned char *bytes = malloc(length ? length : 1);
        CHECK(bytes != NULL);
        memcpy(bytes, address, length);
        struct in_addr out;
        CHECK(!pw_http_sockaddr_ipv4((const struct sockaddr *)bytes,
                                    (socklen_t)length, &out));
        free(bytes);
    }
}

static void test_address_conversion(void) {
    struct in_addr expected = ipv4("192.168.4.1"), out;
    struct sockaddr_in address = {0};
    address.sin_family = AF_INET;
    address.sin_port = htons(8080);
    address.sin_addr = expected;
#ifdef __APPLE__
    address.sin_len = sizeof address;
#endif
    CHECK(pw_http_sockaddr_ipv4((const struct sockaddr *)&address, sizeof address, &out));
    CHECK(out.s_addr == expected.s_addr);
    check_truncated(&address, sizeof address);

    struct sockaddr_in6 mapped = ipv6("::ffff:192.168.4.1");
    mapped.sin6_port = htons(80);
    CHECK(pw_http_sockaddr_ipv4((const struct sockaddr *)&mapped, sizeof mapped, &out));
    CHECK(out.s_addr == expected.s_addr);
    check_truncated(&mapped, sizeof mapped);

    // memcpy-based decoding must not require sockaddr alignment from its caller.
    unsigned char *unaligned = malloc(sizeof mapped + 1);
    CHECK(unaligned != NULL);
    memcpy(unaligned + 1, &mapped, sizeof mapped);
    CHECK(pw_http_sockaddr_ipv4((const struct sockaddr *)(unaligned + 1), sizeof mapped, &out));
    CHECK(out.s_addr == expected.s_addr);
    free(unaligned);

    const char *native[] = {"::1", "::", "2001:db8::1", "::192.168.4.1", "64:ff9b::192.168.4.1"};
    for (size_t i = 0; i < sizeof native / sizeof *native; i++) {
        struct sockaddr_in6 address6 = ipv6(native[i]);
        CHECK(!pw_http_sockaddr_ipv4((const struct sockaddr *)&address6, sizeof address6, &out));
    }
    for (size_t i = 0; i < 12; i++) {
        struct sockaddr_in6 wrong_prefix = mapped;
        wrong_prefix.sin6_addr.s6_addr[i] ^= 1;
        CHECK(!pw_http_sockaddr_ipv4((const struct sockaddr *)&wrong_prefix,
                                    sizeof wrong_prefix, &out));
    }
    struct sockaddr unknown = {0};
    unknown.sa_family = AF_UNSPEC;
    CHECK(!pw_http_sockaddr_ipv4(&unknown, sizeof unknown, &out));
    CHECK(!pw_http_sockaddr_ipv4(NULL, sizeof mapped, &out));
    CHECK(!pw_http_sockaddr_ipv4((const struct sockaddr *)&mapped, sizeof mapped, NULL));
    CHECK(!pw_http_sockaddr_ipv4(NULL, 0, NULL));
}

static void test_host_validation(void) {
    struct in_addr local = ipv4("192.168.4.1");
    CHECK(pw_http_host_matches_ipv4("192.168.4.1", &local));
    CHECK(pw_http_host_matches_ipv4("192.168.4.1:80", &local));
    const char *bad[] = {
        "", "0.0.0.0", "192.168.4.2", "192.168.4.1:8080", "192.168.4.1:443",
        "192.168.4.1:", "192.168.4.1:0", "192.168.4.1:080", "192.168.4.1:+80",
        "192.168.4.1:-80", "192.168.4.1:80:80", "192.168.4.1:80/", "192.168.004.1",
        "192.168.4.01", "192.168.1025", "3232236545", "0xc0a80401", "0300.0250.04.01",
        " 192.168.4.1", "192.168.4.1 ", "192.168.4.1\t", "192.168.4.1\r\n",
        "192.168.4.1.evil.example", "192.168.4.1.", "192.168.4.1@evil.example",
        "evil.example@192.168.4.1", "192.168.4.1:80@evil.example", "http://192.168.4.1",
        "192.168.4.1/", "192.168.4.1#evil", "192.168.4.1?evil", "192.168.4.1%00evil",
        "[192.168.4.1]", "[::ffff:192.168.4.1]", "[::ffff:192.168.4.1]:80", "localhost"
    };
    for (size_t i = 0; i < sizeof bad / sizeof *bad; i++)
        CHECK(!pw_http_host_matches_ipv4(bad[i], &local));
    struct in_addr wildcard = ipv4("0.0.0.0");
    CHECK(!pw_http_host_matches_ipv4("0.0.0.0", &wildcard));
    CHECK(!pw_http_host_matches_ipv4("0.0.0.0:80", &wildcard));
    CHECK(!pw_http_host_matches_ipv4(NULL, &local));
    CHECK(!pw_http_host_matches_ipv4("192.168.4.1", NULL));
}

static void test_real_dual_stack_socket(void) {
    // Bind exactly to mapped loopback, not ::/IN6ADDR_ANY: no LAN listener.
    int listener = socket(AF_INET6, SOCK_STREAM, 0);
    CHECK(listener >= 0);
    int v6only = 0;
    CHECK(setsockopt(listener, IPPROTO_IPV6, IPV6_V6ONLY, &v6only, sizeof v6only) == 0);
    struct sockaddr_in6 address = ipv6("::ffff:127.0.0.1");
    CHECK(bind(listener, (const struct sockaddr *)&address, sizeof address) == 0);
    CHECK(listen(listener, 1) == 0);
    socklen_t length = sizeof address;
    CHECK(getsockname(listener, (struct sockaddr *)&address, &length) == 0);
    CHECK(length == sizeof address);
    CHECK(address.sin6_family == AF_INET6);
    CHECK(IN6_IS_ADDR_V4MAPPED(&address.sin6_addr));
    CHECK(address.sin6_port != 0);
    struct in_addr loopback = ipv4("127.0.0.1"), out;
    CHECK(pw_http_socket_ipv4(listener, false, &out));
    CHECK(out.s_addr == loopback.s_addr);
    CHECK(!pw_http_socket_ipv4(listener, true, &out));

    int client = socket(AF_INET, SOCK_STREAM, 0);
    CHECK(client >= 0);
    struct sockaddr_in target = {0};
    target.sin_family = AF_INET;
    target.sin_addr = loopback;
    target.sin_port = address.sin6_port;
#ifdef __APPLE__
    target.sin_len = sizeof target;
#endif
    CHECK(connect(client, (const struct sockaddr *)&target, sizeof target) == 0);
    struct sockaddr_storage peer = {0};
    length = sizeof peer;
    int accepted = accept(listener, (struct sockaddr *)&peer, &length);
    CHECK(accepted >= 0);
    CHECK(peer.ss_family == AF_INET6);
    CHECK(length == sizeof(struct sockaddr_in6));
    struct sockaddr_in6 peer6;
    memcpy(&peer6, &peer, sizeof peer6);
    CHECK(IN6_IS_ADDR_V4MAPPED(&peer6.sin6_addr));
    CHECK(pw_http_sockaddr_ipv4((const struct sockaddr *)&peer, length, &out));
    CHECK(out.s_addr == loopback.s_addr);

    for (int index = 0; index < 2; index++) {
        int fd = index ? accepted : client;
        CHECK(pw_http_socket_low_latency(fd));
        int enabled = 0;
        socklen_t option_length = sizeof enabled;
        CHECK(getsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &enabled, &option_length) == 0);
        CHECK(enabled != 0); /* Darwin reports the enabled flag as a bitmask. */
        CHECK(pw_http_socket_ipv4(fd, false, &out));
        CHECK(out.s_addr == loopback.s_addr);
        CHECK(pw_http_socket_ipv4(fd, true, &out));
        CHECK(out.s_addr == loopback.s_addr);
        CHECK(!pw_http_socket_ipv4(fd, true, NULL));
    }

    // Reproduce the old sockaddr_in assumption safely: the kernel truncates
    // the buffer and reports the IPv6 size/family, despite an IPv4 connection.
    struct sockaddr_in old_buffer = {0};
    length = sizeof old_buffer;
    CHECK(getpeername(accepted, (struct sockaddr *)&old_buffer, &length) == 0);
    CHECK(length > sizeof old_buffer);
    CHECK(old_buffer.sin_family == AF_INET6);
    CHECK(!pw_http_sockaddr_ipv4((const struct sockaddr *)&old_buffer, sizeof old_buffer, &out));

    CHECK(close(accepted) == 0);
    CHECK(close(client) == 0);
    CHECK(close(listener) == 0);
    CHECK(!pw_http_socket_ipv4(-1, false, &out));
    CHECK(!pw_http_socket_ipv4(-1, true, &out));
    CHECK(!pw_http_socket_ipv4(-1, false, NULL));
    CHECK(!pw_http_socket_low_latency(-1));
    puts("Real IPv4-to-dual-stack loopback connection returned IPv4-mapped IPv6; legacy buffer truncated.");
}

int main(void) {
    // A failed OS connection must not leave CI waiting for a kernel timeout.
    alarm(15);
    test_address_conversion();
    test_host_validation();
    test_real_dual_stack_socket();
    alarm(0);
    printf("HTTP address/Host regression: %u checks passed (ASan/UBSan; no device acceptance).\n", checks);
    return 0;
}
